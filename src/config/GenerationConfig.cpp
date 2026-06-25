#include "schemaforge/config/GenerationConfig.h"

#include <exception>
#include <fstream>
#include <iostream>
#include <regex>
#include <type_traits>

#include "yaml-cpp/yaml.h"

namespace schemaforge {

namespace {

std::string dialect_to_string(SqlDialect dialect) {
  switch (dialect) {
    case SqlDialect::SQLite:
      return "sqlite";
    case SqlDialect::Postgres:
      return "postgres";
  }
  return "sqlite";
}

std::optional<SqlDialect> parse_dialect(const std::string& value) {
  if (value == "sqlite") {
    return SqlDialect::SQLite;
  }
  if (value == "postgres") {
    return SqlDialect::Postgres;
  }
  return std::nullopt;
}

GeneratedValue yaml_scalar_value(const YAML::Node& node) {
  const std::string value = node.as<std::string>();
  const std::string tag = node.Tag();
  if (tag == "!" || tag == "tag:yaml.org,2002:str") {
    return GeneratedValue::text(value);
  }
  if (tag == "tag:yaml.org,2002:bool" || value == "true" || value == "false") {
    return GeneratedValue::boolean(node.as<bool>());
  }
  static const std::regex integer_pattern(R"(^[-+]?\d+$)");
  static const std::regex numeric_pattern(
      R"(^[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?$)");
  if (tag == "tag:yaml.org,2002:int" || std::regex_match(value, integer_pattern)) {
    return GeneratedValue::integer(node.as<std::int64_t>());
  }
  if (tag == "tag:yaml.org,2002:float" || std::regex_match(value, numeric_pattern)) {
    return GeneratedValue::numeric(node.as<double>());
  }
  return GeneratedValue::text(value);
}

void emit_generated_value(YAML::Emitter& yaml, const GeneratedValue& value) {
  value.visit([&yaml](const auto& typed_value) {
    using ValueType = std::decay_t<decltype(typed_value)>;
    if constexpr (std::is_same_v<ValueType, DateValue>) {
      yaml << (std::to_string(typed_value.year) + "-" + std::to_string(typed_value.month) + "-" +
               std::to_string(typed_value.day));
    } else if constexpr (std::is_same_v<ValueType, TimeValue>) {
      yaml << (std::to_string(typed_value.hour) + ":" + std::to_string(typed_value.minute) + ":" +
               std::to_string(typed_value.second));
    } else if constexpr (std::is_same_v<ValueType, DateTimeValue>) {
      yaml << (std::to_string(typed_value.date.year) + "-" +
               std::to_string(typed_value.date.month) + "-" +
               std::to_string(typed_value.date.day) + " " +
               std::to_string(typed_value.time.hour) + ":" +
               std::to_string(typed_value.time.minute) + ":" +
               std::to_string(typed_value.time.second));
    } else if constexpr (!std::is_same_v<ValueType, std::monostate>) {
      yaml << typed_value;
    }
  });
}

}  // namespace

GenerationConfig GenerationConfig::make_default() {
  GenerationConfig generation_config;
  generation_config.default_num_rows = 10;
  generation_config.seed = 42;
  generation_config.realistic = false;
  generation_config.dialect = SqlDialect::SQLite;
  generation_config.schema_path = "schema.sql";
  generation_config.output_file = "output.sql";
  generation_config.output_directory = "";
  generation_config.output_format = "sql";
  generation_config.benchmark_file = "benchmark.yaml";
  generation_config.sqlite_validation = true;
  generation_config.postgres_validation = false;
  generation_config.table_row_counts = {};
  generation_config.table_column_configs = {};
  return generation_config;
}

bool GenerationConfig::apply_init_args(int argc, char* argv[], int start_index,
                                       std::string& config_path) {
  for (int arg_index = start_index; arg_index < argc; ++arg_index) {
    const std::string argument = argv[arg_index];
    if (argument == "--schema" || argument == "--config" || argument == "--seed" ||
        argument == "--default-rows") {
      if (arg_index + 1 >= argc) {
        std::cerr << "Missing value for " << argument << '\n';
        return false;
      }

      const std::string value = argv[++arg_index];
      if (argument == "--schema") {
        schema_path = value;
        continue;
      }

      if (argument == "--config") {
        config_path = value;
        continue;
      }

      if (!apply_option(argument, value)) {
        return false;
      }
      continue;
    }

    if (argument == "--rows") {
      std::cerr << "init does not accept --rows. Edit table row counts in the config file.\n";
      return false;
    }

    if (!argument.empty() && argument.starts_with("--")) {
      std::cerr << "Unknown option: " << argument << '\n';
      return false;
    }

    std::cerr << "Unexpected argument for init: " << argument << '\n';
    return false;
  }

  return true;
}

bool GenerationConfig::apply_generate_args(int argc, char* argv[], int start_index,
                                           std::string& config_path) {
  for (int arg_index = start_index; arg_index < argc; ++arg_index) {
    const std::string argument = argv[arg_index];
    if (argument == "--config") {
      if (arg_index + 1 >= argc) {
        std::cerr << "Missing value for " << argument << '\n';
        return false;
      }

      config_path = argv[++arg_index];
      continue;
    }

    if (!argument.empty() && argument.starts_with("--")) {
      std::cerr << "Unknown option: " << argument << '\n';
      return false;
    }

    std::cerr << "Unexpected argument for generate: " << argument << '\n';
    return false;
  }

  return true;
}

int GenerationConfig::get_row_count(const std::string& table_name) const {
  const auto row_count = table_row_counts.find(table_name);
  if (row_count == table_row_counts.end()) {
    return default_num_rows;
  }
  return row_count->second;
}

const ColumnGenerationConfig* GenerationConfig::get_column_config(
    const std::string& table_name, const std::string& column_name) const {
  const auto table = table_column_configs.find(table_name);
  if (table == table_column_configs.end()) {
    return nullptr;
  }
  const auto column = table->second.find(column_name);
  return column == table->second.end() ? nullptr : &column->second;
}

bool GenerationConfig::apply_option(const std::string& option, const std::string& value) {
  try {
    if (option == "--default-rows") {
      default_num_rows = std::stoi(value);
      return true;
    }

    if (option == "--seed") {
      seed = static_cast<unsigned int>(std::stoul(value));
      return true;
    }

    if (option == "--rows") {
      const std::size_t separator = value.find('=');
      if (separator == std::string::npos || separator == 0 || separator + 1 >= value.size()) {
        std::cerr << "Expected --rows table=count, got: " << value << '\n';
        return false;
      }

      const std::string table_name = value.substr(0, separator);
      const int row_count = std::stoi(value.substr(separator + 1));
      table_row_counts[table_name] = row_count;
      return true;
    }

  } catch (const std::exception& error) {
    std::cerr << "Invalid value for " << option << ": " << value << " (" << error.what() << ")\n";
    return false;
  }

  return false;
}

std::string GenerationConfig::dialect_name() const {
  return dialect_to_string(dialect);
}

void GenerationConfig::write_context_file(const std::vector<std::string>& table_order,
                                          const std::string& path) const {
  YAML::Emitter yaml;
  yaml << YAML::BeginMap;
  yaml << YAML::Key << "schema" << YAML::Value << schema_path;
  yaml << YAML::Key << "dialect" << YAML::Value << dialect_name();
  yaml << YAML::Key << "generation";
  yaml << YAML::Value << YAML::BeginMap;
  yaml << YAML::Key << "seed" << YAML::Value << seed;
  yaml << YAML::Key << "default_rows" << YAML::Value << default_num_rows;
  yaml << YAML::Key << "realistic" << YAML::Value << realistic;
  yaml << YAML::EndMap;

  yaml << YAML::Key << "output";
  yaml << YAML::Value << YAML::BeginMap;
  if (output_format == "csv") {
    yaml << YAML::Key << "directory" << YAML::Value << output_directory;
  } else {
    yaml << YAML::Key << "file" << YAML::Value << output_file;
  }
  yaml << YAML::Key << "format" << YAML::Value << output_format;
  yaml << YAML::EndMap;

  yaml << YAML::Key << "validation";
  yaml << YAML::Value << YAML::BeginMap;
  yaml << YAML::Key << "sqlite" << YAML::Value << sqlite_validation;
  yaml << YAML::Key << "postgres" << YAML::Value << postgres_validation;
  yaml << YAML::EndMap;

  yaml << YAML::Key << "benchmark";
  yaml << YAML::Value << YAML::BeginMap;
  yaml << YAML::Key << "file" << YAML::Value << benchmark_file;
  yaml << YAML::EndMap;

  yaml << YAML::Key << "tables";
  yaml << YAML::Value << YAML::BeginMap;
  for (const auto& table_id : table_order) {
    yaml << YAML::Key << table_id;
    yaml << YAML::Value << YAML::BeginMap;
    yaml << YAML::Key << "rows" << YAML::Value << get_row_count(table_id);
    const auto column_configs = table_column_configs.find(table_id);
    if (column_configs != table_column_configs.end() && !column_configs->second.empty()) {
      yaml << YAML::Key << "columns" << YAML::Value << YAML::BeginMap;
      for (const auto& [column_name, column_config] : column_configs->second) {
        yaml << YAML::Key << column_name << YAML::Value << YAML::BeginMap;
        if (column_config.min_value.has_value()) {
          yaml << YAML::Key << "min" << YAML::Value << column_config.min_value.value();
        }
        if (column_config.max_value.has_value()) {
          yaml << YAML::Key << "max" << YAML::Value << column_config.max_value.value();
        }
        if (column_config.null_probability.has_value()) {
          yaml << YAML::Key << "null_probability" << YAML::Value
               << column_config.null_probability.value();
        }
        if (column_config.has_values) {
          yaml << YAML::Key << "values" << YAML::Value << YAML::BeginSeq;
          for (const auto& value : column_config.values) {
            emit_generated_value(yaml, value);
          }
          yaml << YAML::EndSeq;
        }
        yaml << YAML::EndMap;
      }
      yaml << YAML::EndMap;
    }
    yaml << YAML::EndMap;
  }
  yaml << YAML::EndMap;
  yaml << YAML::EndMap;

  if (!yaml.good()) {
    std::cerr << "Failed to create config YAML: " << yaml.GetLastError() << '\n';
    return;
  }

  std::ofstream config_file(path);
  if (!config_file.is_open()) {
    std::cerr << "Failed to create config file.\n";
    return;
  }

  config_file << yaml.c_str() << '\n';
}

bool GenerationConfig::read_context_file(const std::string& path) {
  try {
    const YAML::Node config = YAML::LoadFile(path);
    if (!config["schema"] || !config["generation"] || !config["tables"]) {
      std::cerr << "Invalid config file: missing 'schema', 'generation', or 'tables' section.\n";
      return false;
    }

    schema_path = config["schema"].as<std::string>();
    if (config["dialect"]) {
      const std::string dialect_value = config["dialect"].as<std::string>();
      const auto parsed_dialect = parse_dialect(dialect_value);
      if (!parsed_dialect.has_value()) {
        std::cerr << "Unsupported SQL dialect: " << dialect_value << '\n';
        return false;
      }
      dialect = parsed_dialect.value();
      if (dialect == SqlDialect::Postgres) {
        sqlite_validation = false;
        postgres_validation = true;
      }
    }

    const YAML::Node generation_node = config["generation"];
    if (generation_node["seed"]) {
      seed = generation_node["seed"].as<unsigned int>();
    }
    if (generation_node["default_rows"]) {
      default_num_rows = generation_node["default_rows"].as<int>();
    } else if (generation_node["default_num_rows"]) {
      default_num_rows = generation_node["default_num_rows"].as<int>();
    }
    if (generation_node["realistic"]) {
      realistic = generation_node["realistic"].as<bool>();
    }

    if (config["output"]) {
      const YAML::Node output_node = config["output"];
      if (output_node["file"]) {
        output_file = output_node["file"].as<std::string>();
      }
      if (output_node["directory"]) {
        output_directory = output_node["directory"].as<std::string>();
      }
      if (output_node["format"]) {
        output_format = output_node["format"].as<std::string>();
      }
    }

    if (output_format == "csv" && output_directory.empty()) {
      std::cerr << "Invalid config file: output.directory is required for CSV output.\n";
      return false;
    }

    if (config["validation"] && config["validation"]["sqlite"]) {
      sqlite_validation = config["validation"]["sqlite"].as<bool>();
    }
    if (config["validation"] && config["validation"]["postgres"]) {
      postgres_validation = config["validation"]["postgres"].as<bool>();
    }

    if (postgres_validation && output_format != "sql" && output_format != "csv" &&
        output_format != "postgres_copy") {
      std::cerr << "Invalid config file: PostgreSQL validation requires SQL, CSV, or postgres_copy "
                   "output.\n";
      return false;
    }
    if (output_format == "postgres_copy" && dialect != SqlDialect::Postgres) {
      std::cerr << "Invalid config file: postgres_copy output requires dialect: postgres.\n";
      return false;
    }
    if (dialect == SqlDialect::SQLite && postgres_validation) {
      std::cerr << "Invalid config file: PostgreSQL validation requires dialect: postgres.\n";
      return false;
    }
    if (dialect == SqlDialect::Postgres && sqlite_validation) {
      std::cerr << "Invalid config file: SQLite validation requires dialect: sqlite.\n";
      return false;
    }

    if (config["benchmark"] && config["benchmark"]["file"]) {
      benchmark_file = config["benchmark"]["file"].as<std::string>();
    }

    table_row_counts.clear();
    table_column_configs.clear();
    const YAML::Node tables_node = config["tables"];
    for (const auto& table_entry : tables_node) {
      const auto table_name = table_entry.first.as<std::string>();
      const YAML::Node table_config = table_entry.second;
      if (table_config["rows"]) {
        table_row_counts[table_name] = table_config["rows"].as<int>();
      }
      if (table_config["columns"]) {
        for (const auto& column_entry : table_config["columns"]) {
          const std::string column_name = column_entry.first.as<std::string>();
          const YAML::Node column_node = column_entry.second;
          ColumnGenerationConfig column_config;
          if (column_node["min"]) {
            column_config.min_value = column_node["min"].as<double>();
          }
          if (column_node["max"]) {
            column_config.max_value = column_node["max"].as<double>();
          }
          if (column_node["null_probability"]) {
            column_config.null_probability = column_node["null_probability"].as<double>();
          }
          if (column_node["values"]) {
            column_config.has_values = true;
            if (!column_node["values"].IsSequence()) {
              throw std::runtime_error("tables." + table_name + ".columns." + column_name +
                                       ".values must be a sequence");
            }
            for (const YAML::Node& value : column_node["values"]) {
              if (!value.IsScalar()) {
                throw std::runtime_error("Configured values must be YAML scalars for " +
                                         table_name + "." + column_name);
              }
              column_config.values.push_back(yaml_scalar_value(value));
            }
          }
          table_column_configs[table_name][column_name] = std::move(column_config);
        }
      }
    }
  } catch (const std::exception& error) {
    std::cerr << "Failed to read config file: " << error.what() << '\n';
    return false;
  }

  return true;
}

}  // namespace schemaforge
