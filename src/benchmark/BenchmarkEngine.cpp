#include "schemaforge/benchmark/BenchmarkEngine.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>

#if defined(__linux__)
#include <sys/resource.h>
#endif

#include "yaml-cpp/yaml.h"

namespace schemaforge {

namespace {

std::string sqlite_status_to_string(SQLiteValidationStatus status) {
  switch (status) {
    case SQLiteValidationStatus::Failed:
      return "failed";
    case SQLiteValidationStatus::Passed:
      return "passed";
    case SQLiteValidationStatus::Skipped:
      return "skipped";
  }

  return "skipped";
}

std::string postgres_status_to_string(PostgresValidationStatus status) {
  switch (status) {
    case PostgresValidationStatus::Failed:
      return "failed";
    case PostgresValidationStatus::Passed:
      return "passed";
    case PostgresValidationStatus::Unavailable:
      return "unavailable";
    case PostgresValidationStatus::Skipped:
      return "skipped";
  }
  return "skipped";
}

double generation_throughput(const BenchmarkReport& report) {
  if (report.generation_time_seconds <= 0.0) {
    return 0.0;
  }

  return static_cast<double>(report.total_rows) / report.generation_time_seconds;
}

void emit_optional_bytes(YAML::Emitter& yaml, const char* key,
                         const std::optional<std::uint64_t>& value) {
  yaml << YAML::Key << key << YAML::Value;
  if (value.has_value()) {
    yaml << *value;
  } else {
    yaml << YAML::Null;
  }
}

}  // namespace

void BenchmarkEngine::record_generated_rows(BenchmarkReport& report,
                                            const std::vector<TableData>& table_data) {
  report.generated_rows.clear();
  report.total_rows = 0;

  for (const auto& table : table_data) {
    std::size_t row_count = 0;
    if (!table.columns.empty()) {
      row_count = table.columns.front().data.size();
    }

    const std::string table_name = table.table->get_table_name();
    report.generated_rows.emplace_back(table_name, row_count);
    report.total_rows += row_count;
  }
}

void BenchmarkEngine::record_configured_rows(BenchmarkReport& report,
                                             const std::vector<TablePtr>& tables,
                                             const GenerationConfig& config) {
  report.generated_rows.clear();
  report.total_rows = 0;

  for (const auto& table : tables) {
    const std::string table_name = table->get_table_name();
    const int row_count = config.get_row_count(table_name);
    const std::size_t normalized_row_count =
        row_count < 0 ? 0 : static_cast<std::size_t>(row_count);
    report.generated_rows.emplace_back(table_name, normalized_row_count);
    report.total_rows += normalized_row_count;
  }
}

std::optional<std::uint64_t> BenchmarkEngine::output_file_size_bytes(
    const std::string& path) {
  std::error_code error;
  const std::uintmax_t file_size = std::filesystem::file_size(path, error);
  if (error || file_size > std::numeric_limits<std::uint64_t>::max()) {
    return std::nullopt;
  }

  return static_cast<std::uint64_t>(file_size);
}

std::optional<std::uint64_t> BenchmarkEngine::output_files_size_bytes(
    const std::vector<std::string>& paths) {
  std::uint64_t total_size = 0;
  for (const auto& path : paths) {
    const auto file_size = output_file_size_bytes(path);
    if (!file_size.has_value() ||
        total_size > std::numeric_limits<std::uint64_t>::max() - *file_size) {
      return std::nullopt;
    }
    total_size += *file_size;
  }
  return total_size;
}

std::optional<std::uint64_t> BenchmarkEngine::peak_process_memory_bytes() {
#if defined(__linux__)
  rusage usage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0) {
    return std::nullopt;
  }

  constexpr std::uint64_t bytes_per_kibibyte = 1024;
  const auto peak_kibibytes = static_cast<std::uint64_t>(usage.ru_maxrss);
  if (peak_kibibytes >
      std::numeric_limits<std::uint64_t>::max() / bytes_per_kibibyte) {
    return std::nullopt;
  }

  return peak_kibibytes * bytes_per_kibibyte;
#else
  return std::nullopt;
#endif
}

bool BenchmarkEngine::write_report(const BenchmarkReport& report, const std::string& path) {
  YAML::Emitter yaml;
  yaml << YAML::BeginMap;
  yaml << YAML::Key << "generation";
  yaml << YAML::Value << YAML::BeginMap;
  yaml << YAML::Key << "rows";
  yaml << YAML::Value << YAML::BeginMap;
  for (const auto& [table_name, row_count] : report.generated_rows) {
    yaml << YAML::Key << table_name << YAML::Value << row_count;
  }
  yaml << YAML::EndMap;
  yaml << YAML::Key << "total_rows" << YAML::Value << report.total_rows;
  yaml << YAML::Key << "time_seconds" << YAML::Value << report.generation_time_seconds;
  yaml << YAML::Key << "throughput_rows_per_second" << YAML::Value
       << generation_throughput(report);
  emit_optional_bytes(yaml, "output_file_size_bytes", report.output_file_size_bytes);
  yaml << YAML::EndMap;

  yaml << YAML::Key << "validation";
  yaml << YAML::Value << YAML::BeginMap;
  yaml << YAML::Key << "sqlite" << YAML::Value
       << sqlite_status_to_string(report.sqlite_validation_status);
  yaml << YAML::Key << "time_seconds" << YAML::Value << report.validation_time_seconds;
  yaml << YAML::Key << "postgres" << YAML::Value
       << postgres_status_to_string(report.postgres_validation_status);
  yaml << YAML::Key << "postgres_time_seconds" << YAML::Value
       << report.postgres_validation_time_seconds;
  yaml << YAML::EndMap;

  yaml << YAML::Key << "total";
  yaml << YAML::Value << YAML::BeginMap;
  yaml << YAML::Key << "command_time_seconds" << YAML::Value
       << report.total_command_time_seconds;
  emit_optional_bytes(yaml, "peak_process_memory_bytes", report.peak_process_memory_bytes);
  yaml << YAML::EndMap;
  yaml << YAML::EndMap;

  if (!yaml.good()) {
    std::cerr << "Failed to create benchmark YAML: " << yaml.GetLastError() << '\n';
    return false;
  }

  std::ofstream benchmark_file(path);
  if (!benchmark_file.is_open()) {
    std::cerr << "Failed to open benchmark file: " << path << '\n';
    return false;
  }

  benchmark_file << yaml.c_str() << '\n';
  return true;
}

}  // namespace schemaforge
