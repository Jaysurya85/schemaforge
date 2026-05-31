#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "schemaforge/generator/GenerationConfig.h"
#include "schemaforge/generator/IntGenerator.h"
#include "schemaforge/generator/RandomEngine.h"
#include "schemaforge/schema/Table.h"

namespace schemaforge {

class KeyRegistry {
 private:
  struct IntRangeKeySource {
    int start;
    int count;
  };

  struct KeyRef {
    const Table* table;
    std::vector<const Column*> columns;

    bool operator==(const KeyRef& other) const;
  };

  struct KeyRefHash {
    std::size_t operator()(const KeyRef& key) const;
  };

  std::unordered_map<KeyRef, IntRangeKeySource, KeyRefHash> int_range_sources;
  static KeyRef make_key(const Table* table, const std::vector<Column*>& columns);

 public:
  KeyRegistry() = default;

  void register_int_range(const Table* table, const std::vector<Column*>& columns, int start,
                          int count);

  [[nodiscard]] std::vector<Data> random_key(const Table* table,
                                             const std::vector<Column*>& columns,
                                             RandomEngine& random_engine) const;

  [[nodiscard]] std::vector<Data> key_at_row(const Table* table,
                                             const std::vector<Column*>& columns,
                                             std::size_t row_index) const;

  static KeyRegistry build_from_tables(const std::vector<TablePtr>& tables,
                                       const GenerationConfig& config);
};

}  // namespace schemaforge
