#include "schemaforge/config/GenerationConfig.h"

#include <exception>
#include <fstream>
#include <iostream>

#include "yaml-cpp/yaml.h"

namespace schemaforge {

GenerationConfig GenerationConfig::make_default() {
  GenerationConfig generation_config;
  generation_config.default_num_rows = 10;
  generation_config.seed = 42;
  generation_config.schema_path = "schema.sql";
  generation_config.output_file = "output.sql";
  generation_config.output_format = "sql";
  generation_config.benchmark_file = "benchmark.yaml";
  generation_config.sqlite_validation = true;
  generation_config.table_row_counts = {};
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

void GenerationConfig::write_context_file(const std::vector<std::string>& table_order,
                                          const std::string& path) const {
  YAML::Emitter yaml;
  yaml << YAML::BeginMap;
  yaml << YAML::Key << "schema" << YAML::Value << schema_path;
  yaml << YAML::Key << "generation";
  yaml << YAML::Value << YAML::BeginMap;
  yaml << YAML::Key << "seed" << YAML::Value << seed;
  yaml << YAML::Key << "default_rows" << YAML::Value << default_num_rows;
  yaml << YAML::EndMap;

  yaml << YAML::Key << "output";
  yaml << YAML::Value << YAML::BeginMap;
  yaml << YAML::Key << "file" << YAML::Value << output_file;
  yaml << YAML::Key << "format" << YAML::Value << output_format;
  yaml << YAML::EndMap;

  yaml << YAML::Key << "validation";
  yaml << YAML::Value << YAML::BeginMap;
  yaml << YAML::Key << "sqlite" << YAML::Value << sqlite_validation;
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

    const YAML::Node generation_node = config["generation"];
    if (generation_node["seed"]) {
      seed = generation_node["seed"].as<unsigned int>();
    }
    if (generation_node["default_rows"]) {
      default_num_rows = generation_node["default_rows"].as<int>();
    } else if (generation_node["default_num_rows"]) {
      default_num_rows = generation_node["default_num_rows"].as<int>();
    }

    if (config["output"]) {
      const YAML::Node output_node = config["output"];
      if (output_node["file"]) {
        output_file = output_node["file"].as<std::string>();
      }
      if (output_node["format"]) {
        output_format = output_node["format"].as<std::string>();
      }
    }

    if (config["validation"] && config["validation"]["sqlite"]) {
      sqlite_validation = config["validation"]["sqlite"].as<bool>();
    }

    if (config["benchmark"] && config["benchmark"]["file"]) {
      benchmark_file = config["benchmark"]["file"].as<std::string>();
    }

    table_row_counts.clear();
    const YAML::Node tables_node = config["tables"];
    for (const auto& table_entry : tables_node) {
      const auto table_name = table_entry.first.as<std::string>();
      const YAML::Node table_config = table_entry.second;
      if (table_config["rows"]) {
        table_row_counts[table_name] = table_config["rows"].as<int>();
      }
    }
  } catch (const std::exception& error) {
    std::cerr << "Failed to read config file: " << error.what() << '\n';
    return false;
  }

  return true;
}

}  // namespace schemaforge
