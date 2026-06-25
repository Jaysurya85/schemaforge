#pragma once
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "schemaforge/generator/GeneratedValue.h"

namespace schemaforge {

struct ColumnGenerationConfig {
  std::optional<double> min_value;
  std::optional<double> max_value;
  std::vector<GeneratedValue> values;
  bool has_values{false};
  std::optional<double> null_probability;
};

using ColumnGenerationConfigs = std::unordered_map<std::string, ColumnGenerationConfig>;

struct GenerationConfig {
  int default_num_rows{10};
  unsigned int seed{42};
  bool realistic{false};
  std::string schema_path{"schema.sql"};
  std::string output_file{"output.sql"};
  std::string output_directory;
  std::string output_format{"sql"};
  std::string benchmark_file{"benchmark.yaml"};
  bool sqlite_validation{true};
  bool postgres_validation{false};
  std::unordered_map<std::string, int> table_row_counts;
  std::unordered_map<std::string, ColumnGenerationConfigs> table_column_configs;

  static GenerationConfig make_default();

  [[nodiscard]] bool apply_init_args(int argc, char* argv[], int start_index,
                                     std::string& config_path);
  [[nodiscard]] bool apply_generate_args(int argc, char* argv[], int start_index,
                                         std::string& config_path);
  [[nodiscard]] int get_row_count(const std::string& table_name) const;
  [[nodiscard]] const ColumnGenerationConfig* get_column_config(
      const std::string& table_name, const std::string& column_name) const;
  [[nodiscard]] bool apply_option(const std::string& option, const std::string& value);
  void write_context_file(const std::vector<std::string>& table_order,
                          const std::string& path = "schemaforge.yaml") const;

  [[nodiscard]] bool read_context_file(const std::string& path);
};

}  // namespace schemaforge
