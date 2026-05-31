#include "schemaforge/generator/GenerationConfig.h"

#include <exception>
#include <fstream>
#include <iostream>

#include "yaml-cpp/yaml.h"

namespace schemaforge {

GenerationConfig GenerationConfig::make_default() {
  GenerationConfig generation_config;
  generation_config.default_num_rows = 10;
  generation_config.seed = 42;
  generation_config.table_row_counts = {{"users", 10}, {"orders", 25}};
  return generation_config;
}

int GenerationConfig::get_row_count(const std::string& table_name) const {
  const auto row_count = table_row_counts.find(table_name);
  if (row_count == table_row_counts.end()) {
    return default_num_rows;
  }
  return row_count->second;
}

bool GenerationConfig::apply_cli_args(int argc, char* argv[], std::string& schema_path) {
  for (int arg_index = 1; arg_index < argc; ++arg_index) {
    const std::string argument = argv[arg_index];
    if (argument == "--default-rows" || argument == "--seed" || argument == "--rows") {
      if (arg_index + 1 >= argc) {
        std::cerr << "Missing value for " << argument << '\n';
        return false;
      }

      if (!apply_option(argument, argv[++arg_index])) {
        return false;
      }
      continue;
    }

    if (!argument.empty() && argument.starts_with("--")) {
      std::cerr << "Unknown option: " << argument << '\n';
      return false;
    }

    schema_path = argument;
  }

  return true;
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
  yaml << YAML::Key << "generation";
  yaml << YAML::Value << YAML::BeginMap;
  yaml << YAML::Key << "seed" << YAML::Value << seed;
  yaml << YAML::Key << "default_num_rows" << YAML::Value << default_num_rows;
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

}  // namespace schemaforge
