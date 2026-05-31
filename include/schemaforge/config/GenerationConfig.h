#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace schemaforge {

struct GenerationConfig {
  int default_num_rows{10};
  unsigned int seed{42};
  std::string schema_path{"schema.sql"};
  std::string output_file{"output.sql"};
  std::string output_format{"sql"};
  bool sqlite_validation{true};
  std::unordered_map<std::string, int> table_row_counts;

  static GenerationConfig make_default();

  [[nodiscard]] bool apply_init_args(int argc, char* argv[], int start_index,
                                     std::string& config_path);
  [[nodiscard]] bool apply_generate_args(int argc, char* argv[], int start_index,
                                         std::string& config_path);
  [[nodiscard]] int get_row_count(const std::string& table_name) const;
  [[nodiscard]] bool apply_option(const std::string& option, const std::string& value);
  void write_context_file(const std::vector<std::string>& table_order,
                          const std::string& path = "schemaforge.yaml") const;

  [[nodiscard]] bool read_context_file(const std::string& path);
};

}  // namespace schemaforge
