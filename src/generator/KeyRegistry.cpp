#include "schemaforge/generator/KeyRegistry.h"

#include <algorithm>
#include <stdexcept>

namespace schemaforge {

namespace {

bool is_integer_type(DataType data_type) {
  return data_type == DataType::INT || data_type == DataType::BIGINT;
}

const Column* find_column(const Table& table, const std::string& column_name,
                          std::vector<Column>& columns) {
  columns = table.get_columns();
  const auto column = std::ranges::find_if(columns, [&column_name](const Column& candidate) {
    return candidate.get_column_name() == column_name;
  });

  if (column == columns.end()) {
    return nullptr;
  }

  return &*column;
}

}  // namespace

std::string KeyRegistry::make_key(const std::string& table_name,
                                  const std::vector<std::string>& columns) {
  std::string key = table_name;
  key += "(";
  for (std::size_t column_index = 0; column_index < columns.size(); ++column_index) {
    if (column_index > 0) {
      key += ",";
    }
    key += columns[column_index];
  }
  key += ")";
  return key;
}

void KeyRegistry::register_int_range(const std::string& table_name,
                                     const std::vector<std::string>& columns, int start,
                                     int count) {
  int_range_sources[make_key(table_name, columns)] = IntRangeKeySource{.start = start,
                                                                       .count = count};
}

std::vector<Data> KeyRegistry::random_key(const std::string& table_name,
                                          const std::vector<std::string>& columns,
                                          RandomEngine& random_engine) const {
  const auto key_source = int_range_sources.find(make_key(table_name, columns));
  if (key_source == int_range_sources.end()) {
    throw std::runtime_error("No key source registered for referenced key '" + table_name + "'");
  }

  if (key_source->second.count <= 0) {
    throw std::runtime_error("Referenced key source for table '" + table_name + "' has no values");
  }

  const int offset = random_engine.next_int(0, key_source->second.count - 1);
  return {std::to_string(key_source->second.start + offset)};
}

std::vector<Data> KeyRegistry::key_at_row(const std::string& table_name,
                                          const std::vector<std::string>& columns,
                                          std::size_t row_index) const {
  const auto key_source = int_range_sources.find(make_key(table_name, columns));
  if (key_source == int_range_sources.end()) {
    throw std::runtime_error("No key source registered for referenced key '" + table_name + "'");
  }

  if (row_index >= static_cast<std::size_t>(key_source->second.count)) {
    throw std::runtime_error("Referenced key source for table '" + table_name +
                             "' does not have enough values");
  }

  return {std::to_string(key_source->second.start + static_cast<int>(row_index))};
}

KeyRegistry KeyRegistry::build_from_tables(const std::vector<Table>& tables,
                                           const GenerationConfig& config) {
  KeyRegistry key_registry;

  for (const auto& table : tables) {
    const int row_count = config.get_row_count(table.get_table_name());
    for (const auto& constraint : table.get_table_constraints()) {
      if (constraint.columnNames.size() != 1) {
        continue;
      }

      if (constraint.type != ConstraintType::PrimaryKey &&
          constraint.type != ConstraintType::Unique) {
        continue;
      }

      std::vector<Column> columns;
      const Column* column = find_column(table, constraint.columnNames.front(), columns);
      if (column == nullptr || !is_integer_type(column->get_column_type().data_type)) {
        continue;
      }

      key_registry.register_int_range(table.get_table_name(), constraint.columnNames, 1,
                                      row_count);
    }
  }

  return key_registry;
}

}  // namespace schemaforge
