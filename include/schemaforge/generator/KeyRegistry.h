#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "schemaforge/config/GenerationConfig.h"
#include "schemaforge/generator/GeneratedValue.h"
#include "schemaforge/generator/IntGenerator.h"
#include "schemaforge/generator/RandomEngine.h"
#include "schemaforge/schema/Table.h"

namespace schemaforge {

class KeyRegistry {
 public:
  enum class PatternKeyKind {
    TableKey,
    ColumnKey,
    Email,
    Char
  };

 private:
  struct IntRangeKeySource {
    int start;
    int count;
  };

  struct PatternKeySource {
    PatternKeyKind kind;
    std::string prefix;
    int length;
    int count;
  };

  struct AllowedValuesKeySource {
    std::vector<GeneratedValue> values;
  };

  struct TupleKeySource {
    std::vector<std::vector<GeneratedValue>> values;
  };

  using KeySource =
      std::variant<IntRangeKeySource, PatternKeySource, AllowedValuesKeySource, TupleKeySource>;

  struct KeyRef {
    const Table* table;
    std::vector<const Column*> columns;

    bool operator==(const KeyRef& other) const;
  };

  struct KeyRefHash {
    std::size_t operator()(const KeyRef& key) const;
  };

  std::unordered_map<KeyRef, KeySource, KeyRefHash> key_sources;
  static KeyRef make_key(const Table* table, const std::vector<Column*>& columns);
  static GeneratedValue value_at_row(const PatternKeySource& source, std::size_t row_index);

 public:
  KeyRegistry() = default;

  void register_int_range(const Table* table, const std::vector<Column*>& columns, int start,
                          int count);
  void register_pattern(const Table* table, const std::vector<Column*>& columns,
                        PatternKeyKind kind, std::string prefix, int length, int count);

  [[nodiscard]] std::vector<GeneratedValue> random_key(const Table* table,
                                                       const std::vector<Column*>& columns,
                                                       RandomEngine& random_engine) const;

  [[nodiscard]] std::vector<GeneratedValue> key_at_row(const Table* table,
                                                       const std::vector<Column*>& columns,
                                                       std::size_t row_index) const;

  static KeyRegistry build_from_tables(const std::vector<TablePtr>& tables,
                                       const GenerationConfig& config);
};

}  // namespace schemaforge
