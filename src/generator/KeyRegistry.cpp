#include "schemaforge/generator/KeyRegistry.h"

#include <algorithm>
#include <functional>
#include <stdexcept>

namespace schemaforge {

namespace {

bool is_integer_type(DataType data_type) {
  return data_type == DataType::INT || data_type == DataType::BIGINT;
}

bool is_single_integer_key(const TableConstraint& constraint) {
  if (constraint.columns.size() != 1) {
    return false;
  }

  if (constraint.type != ConstraintType::PrimaryKey && constraint.type != ConstraintType::Unique) {
    return false;
  }

  const Column* column = constraint.columns.front();
  return column != nullptr && is_integer_type(column->get_column_type().data_type);
}

std::string key_name(const Table* table) {
  if (table == nullptr) {
    return "<null table>";
  }
  return table->get_table_name();
}

}  // namespace

bool KeyRegistry::KeyRef::operator==(const KeyRef& other) const {
  return table == other.table && columns == other.columns;
}

std::size_t KeyRegistry::KeyRefHash::operator()(const KeyRef& key) const {
  std::size_t hash = std::hash<const Table*>{}(key.table);
  for (const Column* column : key.columns) {
    const std::size_t column_hash = std::hash<const Column*>{}(column);
    hash ^= column_hash + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  }
  return hash;
}

KeyRegistry::KeyRef KeyRegistry::make_key(const Table* table,
                                          const std::vector<Column*>& columns) {
  std::vector<const Column*> key_columns;
  key_columns.reserve(columns.size());
  for (const Column* column : columns) {
    key_columns.push_back(column);
  }
  return KeyRef{.table = table, .columns = std::move(key_columns)};
}

void KeyRegistry::register_int_range(const Table* table, const std::vector<Column*>& columns,
                                     int start, int count) {
  int_range_sources[make_key(table, columns)] = IntRangeKeySource{.start = start, .count = count};
}

std::vector<Data> KeyRegistry::random_key(const Table* table, const std::vector<Column*>& columns,
                                          RandomEngine& random_engine) const {
  const auto key_source = int_range_sources.find(make_key(table, columns));
  if (key_source == int_range_sources.end()) {
    throw std::runtime_error("No key source registered for referenced key '" + key_name(table) +
                             "'");
  }

  if (key_source->second.count <= 0) {
    throw std::runtime_error("Referenced key source for table '" + key_name(table) +
                             "' has no values");
  }

  const int offset = random_engine.next_int(0, key_source->second.count - 1);
  return {std::to_string(key_source->second.start + offset)};
}

std::vector<Data> KeyRegistry::key_at_row(const Table* table, const std::vector<Column*>& columns,
                                          std::size_t row_index) const {
  const auto key_source = int_range_sources.find(make_key(table, columns));
  if (key_source == int_range_sources.end()) {
    throw std::runtime_error("No key source registered for referenced key '" + key_name(table) +
                             "'");
  }

  if (row_index >= static_cast<std::size_t>(key_source->second.count)) {
    throw std::runtime_error("Referenced key source for table '" + key_name(table) +
                             "' does not have enough values");
  }

  return {std::to_string(key_source->second.start + static_cast<int>(row_index))};
}

KeyRegistry KeyRegistry::build_from_tables(const std::vector<TablePtr>& tables,
                                           const GenerationConfig& config) {
  KeyRegistry key_registry;

  for (const auto& table_ptr : tables) {
    const Table* table = table_ptr.get();
    const int row_count = config.get_row_count(table->get_table_name());
    for (const auto& constraint : table->get_table_constraints()) {
      if (!is_single_integer_key(constraint)) {
        continue;
      }

      key_registry.register_int_range(table, constraint.columns, 1, row_count);
    }
  }

  return key_registry;
}

}  // namespace schemaforge
