#include "schemaforge/validation/PostgresDockerValidator.h"

#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace schemaforge {
namespace {

constexpr const char* postgres_image = "postgres:17-alpine";
constexpr const char* database_name = "schemaforge";
constexpr const char* database_user = "postgres";

struct ProcessResult {
  int exit_code;
  std::string output;
};

ProcessResult run_process(
    const std::vector<std::string>& arguments,
    std::chrono::milliseconds timeout = std::chrono::minutes(5)) {
#if defined(__unix__) || defined(__APPLE__)
  if (arguments.empty()) {
    return {127, "Missing command"};
  }

  int output_pipe[2];
  if (pipe(output_pipe) != 0) {
    return {127, "Failed to create process output pipe"};
  }

  const pid_t child = fork();
  if (child < 0) {
    close(output_pipe[0]);
    close(output_pipe[1]);
    return {127, "Failed to create process"};
  }

  if (child == 0) {
    close(output_pipe[0]);
    dup2(output_pipe[1], STDOUT_FILENO);
    dup2(output_pipe[1], STDERR_FILENO);
    close(output_pipe[1]);

    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (const auto& argument : arguments) {
      argv.push_back(const_cast<char*>(argument.c_str()));
    }
    argv.push_back(nullptr);
    execvp(argv.front(), argv.data());
    _exit(127);
  }

  close(output_pipe[1]);
  const int current_flags = fcntl(output_pipe[0], F_GETFL, 0);
  if (current_flags >= 0) {
    fcntl(output_pipe[0], F_SETFL, current_flags | O_NONBLOCK);
  }

  std::string output;
  char buffer[4096];
  const auto start = std::chrono::steady_clock::now();
  int status = 0;
  while (true) {
    while (true) {
      const ssize_t bytes_read = read(output_pipe[0], buffer, sizeof(buffer));
      if (bytes_read > 0) {
        output.append(buffer, static_cast<std::size_t>(bytes_read));
        continue;
      }
      if (bytes_read < 0 && errno == EINTR) {
        continue;
      }
      break;
    }

    const pid_t wait_result = waitpid(child, &status, WNOHANG);
    if (wait_result == child) {
      close(output_pipe[0]);
      if (WIFEXITED(status)) {
        return {WEXITSTATUS(status), std::move(output)};
      }
      if (WIFSIGNALED(status)) {
        return {128 + WTERMSIG(status), std::move(output)};
      }
      return {127, std::move(output)};
    }
    if (wait_result < 0 && errno != EINTR) {
      close(output_pipe[0]);
      return {127, std::move(output)};
    }

    if (std::chrono::steady_clock::now() - start >= timeout) {
      kill(child, SIGKILL);
      waitpid(child, &status, WNOHANG);
      close(output_pipe[0]);
      output += "Command timed out";
      return {124, std::move(output)};
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
#else
  (void)arguments;
  (void)timeout;
  return {127, "Docker validation is unavailable on this platform"};
#endif
}

std::string trim(const std::string& value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
    ++begin;
  }
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
    --end;
  }
  return value.substr(begin, end - begin);
}

std::string quote_identifier(const std::string& identifier) {
  std::string quoted = "\"";
  for (const char character : identifier) {
    quoted += character == '"' ? "\"\"" : std::string(1, character);
  }
  return quoted + "\"";
}

std::string quote_literal(const std::string& value) {
  std::string quoted = "'";
  for (const char character : value) {
    quoted += character == '\'' ? "''" : std::string(1, character);
  }
  return quoted + "'";
}

PostgresDockerValidationResult failed(const std::string& context,
                                      const ProcessResult& process) {
  std::string error = context;
  const std::string detail = trim(process.output);
  if (!detail.empty()) {
    error += ": " + detail;
  }
  return {PostgresDockerValidationStatus::Failed, {std::move(error)}};
}

class ContainerGuard {
 private:
  std::string name;
  bool active{false};

 public:
  explicit ContainerGuard(std::string name) : name(std::move(name)) {}
  void activate() { active = true; }
  ~ContainerGuard() {
    if (active) {
      run_process({"docker", "rm", "--force", name}, std::chrono::seconds(15));
    }
  }
};

class TemporaryFile {
 private:
  std::filesystem::path path;

 public:
  explicit TemporaryFile(std::filesystem::path path) : path(std::move(path)) {}
  ~TemporaryFile() {
    std::error_code error;
    std::filesystem::remove(path, error);
  }
  [[nodiscard]] const std::filesystem::path& get() const { return path; }
};

ProcessResult docker_exec(const std::string& container,
                          const std::vector<std::string>& command) {
  std::vector<std::string> arguments{"docker", "exec", container};
  arguments.insert(arguments.end(), command.begin(), command.end());
  return run_process(arguments);
}

ProcessResult run_psql_file(const std::string& container, const std::string& path) {
  return docker_exec(container, {"psql", "--set", "ON_ERROR_STOP=1", "--username",
                                 database_user, "--dbname", database_name, "--file", path});
}

std::string csv_load_script(const std::vector<TablePtr>& tables) {
  std::ostringstream sql;
  sql << "BEGIN;\n";
  for (const auto& table_ptr : tables) {
    const Table& table = *table_ptr;
    sql << "COPY " << quote_identifier(table.get_table_name()) << " (";
    const auto& columns = table.get_columns();
    for (std::size_t index = 0; index < columns.size(); ++index) {
      if (index > 0) {
        sql << ", ";
      }
      sql << quote_identifier(columns[index]->get_column_name());
    }
    const std::string csv_path = "/tmp/schemaforge_csv/" + table.get_table_name() + ".csv";
    sql << ") FROM " << quote_literal(csv_path) << " WITH (FORMAT csv, HEADER true);\n";
  }
  sql << "COMMIT;\n";
  return sql.str();
}

}  // namespace

PostgresDockerValidationResult PostgresDockerValidator::validate(
    const std::string& schema_path, const std::vector<TablePtr>& tables,
    const GenerationConfig& config) {
  const ProcessResult docker_version =
      run_process({"docker", "version", "--format", "{{.Server.Version}}"},
                  std::chrono::seconds(10));
  if (docker_version.exit_code != 0) {
    return {PostgresDockerValidationStatus::Unavailable,
            {"Docker daemon unavailable: " + trim(docker_version.output)}};
  }

#if defined(__unix__) || defined(__APPLE__)
  const std::string container = "schemaforge-postgres-" + std::to_string(getpid());
#else
  const std::string container = "schemaforge-postgres";
#endif
  ContainerGuard container_guard(container);
  container_guard.activate();
  const ProcessResult start = run_process(
      {"docker", "run", "--detach", "--rm", "--name", container, "--env",
       "POSTGRES_PASSWORD=schemaforge", "--env", "POSTGRES_DB=schemaforge", postgres_image});
  if (start.exit_code != 0) {
    return {PostgresDockerValidationStatus::Unavailable,
            {"Could not start PostgreSQL Docker container: " + trim(start.output)}};
  }
  bool ready = false;
  ProcessResult readiness{1, "PostgreSQL did not become ready"};
  for (int attempt = 0; attempt < 40; ++attempt) {
    readiness = docker_exec(container,
                            {"pg_isready", "--username", database_user, "--dbname", database_name});
    if (readiness.exit_code == 0) {
      ready = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
  }
  if (!ready) {
    return failed("PostgreSQL container did not become ready", readiness);
  }

  ProcessResult process =
      run_process({"docker", "cp", schema_path, container + ":/tmp/schema.sql"});
  if (process.exit_code != 0) {
    return failed("Failed to copy schema into PostgreSQL container", process);
  }
  process = run_psql_file(container, "/tmp/schema.sql");
  if (process.exit_code != 0) {
    return failed("PostgreSQL schema compatibility validation failed", process);
  }

  if (config.output_format == "sql") {
    process = run_process(
        {"docker", "cp", config.output_file, container + ":/tmp/generated.sql"});
    if (process.exit_code != 0) {
      return failed("Failed to copy SQL output into container", process);
    }
    process = run_psql_file(container, "/tmp/generated.sql");
  } else if (config.output_format == "postgres_copy") {
    process = run_process(
        {"docker", "cp", config.output_file, container + ":/tmp/generated.copy.sql"});
    if (process.exit_code != 0) {
      return failed("Failed to copy PostgreSQL COPY output into container", process);
    }
    process = run_psql_file(container, "/tmp/generated.copy.sql");
  } else if (config.output_format == "csv") {
    process = docker_exec(container, {"mkdir", "-p", "/tmp/schemaforge_csv"});
    if (process.exit_code != 0) {
      return failed("Failed to prepare CSV directory in PostgreSQL container", process);
    }
    process = run_process(
        {"docker", "cp", config.output_directory + "/.", container + ":/tmp/schemaforge_csv"});
    if (process.exit_code != 0) {
      return failed("Failed to copy CSV output into PostgreSQL container", process);
    }

    const std::filesystem::path load_path =
        std::filesystem::temp_directory_path() /
        ("schemaforge-postgres-load-" + container + ".sql");
    TemporaryFile load_file(load_path);
    std::ofstream load_sql(load_file.get(), std::ios::binary);
    load_sql << csv_load_script(tables);
    load_sql.close();
    if (!load_sql) {
      return {PostgresDockerValidationStatus::Failed,
              {"Failed to create temporary PostgreSQL CSV load script"}};
    }
    process = run_process(
        {"docker", "cp", load_file.get().string(), container + ":/tmp/load-csv.sql"});
    if (process.exit_code != 0) {
      return failed("Failed to copy CSV load script into PostgreSQL container", process);
    }
    process = run_psql_file(container, "/tmp/load-csv.sql");
  } else {
    return {PostgresDockerValidationStatus::Failed,
            {"PostgreSQL Docker validation supports SQL, CSV, and postgres_copy output"}};
  }

  if (process.exit_code != 0) {
    return failed("PostgreSQL generated-data import failed", process);
  }

  for (const auto& table_ptr : tables) {
    const std::string query =
        "SELECT COUNT(*) FROM " + quote_identifier(table_ptr->get_table_name()) + ";";
    process = docker_exec(container, {"psql", "--tuples-only", "--no-align", "--username",
                                      database_user, "--dbname", database_name, "--command", query});
    if (process.exit_code != 0) {
      return failed("Failed to verify PostgreSQL row count", process);
    }
    const std::string actual = trim(process.output);
    const std::string expected =
        std::to_string(config.get_row_count(table_ptr->get_table_name()));
    if (actual != expected) {
      return {PostgresDockerValidationStatus::Failed,
              {"PostgreSQL row count mismatch for table '" + table_ptr->get_table_name() +
               "': expected " + expected + ", got " + actual}};
    }
  }

  return {PostgresDockerValidationStatus::Passed, {}};
}

}  // namespace schemaforge
