#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "schemaforge/generator/IntGenerator.h"
#include "schemaforge/generator/GenerationConfig.h"
#include "schemaforge/generator/RandomEngine.h"
#include "schemaforge/schema/Table.h"

namespace schemaforge {

class KeyRegistry {
 private:
  struct IntRangeKeySource {
    int start;
    int count;
  };

  std::unordered_map<std::string, IntRangeKeySource> int_range_sources;

  static std::string make_key(const std::string& table_name,
                              const std::vector<std::string>& columns);

 public:
  KeyRegistry() = default;

  void register_int_range(const std::string& table_name, const std::vector<std::string>& columns,
                          int start, int count);

  [[nodiscard]] std::vector<Data> random_key(const std::string& table_name,
                                             const std::vector<std::string>& columns,
                                             RandomEngine& random_engine) const;

  [[nodiscard]] std::vector<Data> key_at_row(const std::string& table_name,
                                             const std::vector<std::string>& columns,
                                             std::size_t row_index) const;

  static KeyRegistry build_from_tables(const std::vector<Table>& tables,
                                       const GenerationConfig& config);
};

}  // namespace schemaforge
