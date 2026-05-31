#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace schemaforge {

struct GenerationConfig {
  int default_num_rows{10};
  unsigned int seed{42};
  std::unordered_map<std::string, int> table_row_counts;

  static GenerationConfig make_default();

  [[nodiscard]] bool apply_cli_args(int argc, char* argv[], std::string& schema_path);
  [[nodiscard]] int get_row_count(const std::string& table_name) const;
  [[nodiscard]] bool apply_option(const std::string& option, const std::string& value);
  void write_context_file(const std::vector<std::string>& table_order,
                          const std::string& path = "context.yaml") const;
};

}  // namespace schemaforge
