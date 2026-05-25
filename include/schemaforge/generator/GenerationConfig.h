#pragma once
#include <string>
#include <unordered_map>

namespace schemaforge {

struct GenerationConfig {
  int default_num_rows{10};
  unsigned int seed{42};
  std::unordered_map<std::string, int> table_row_counts;

  [[nodiscard]] int get_row_count(const std::string& table_name) const {
    const auto row_count = table_row_counts.find(table_name);
    if (row_count == table_row_counts.end()) {
      return default_num_rows;
    }
    return row_count->second;
  }
};

}  // namespace schemaforge
