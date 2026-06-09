#include "schemaforge/generator/GenerationPlan.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <type_traits>
#include <typeinfo>
#include <unordered_set>
#include <variant>

#include "schemaforge/generator/ValueGenerator.h"

namespace schemaforge {

namespace {

const ForeignKey* find_single_column_foreign_key(const Table& table, const Column* column) {
  for (const auto& foreign_key : table.get_foreign_keys()) {
    if (foreign_key.local_columns.size() == 1 && foreign_key.referenced_columns.size() == 1 &&
        foreign_key.local_columns.front() == column) {
      return &foreign_key;
    }
  }
  return nullptr;
}

bool has_single_column_constraint(const Table& table, const Column* column,
                                  ConstraintType constraint_type) {
  for (const auto& constraint : table.get_table_constraints()) {
    if (constraint.type == constraint_type && constraint.columns.size() == 1 &&
        constraint.columns.front() == column) {
      return true;
    }
  }
  return false;
}

bool has_already_unique_member(const Table& table, const TableConstraint& constraint) {
  return std::ranges::any_of(constraint.columns, [&table](const Column* column) {
    return has_single_column_constraint(table, column, ConstraintType::PrimaryKey) ||
           has_single_column_constraint(table, column, ConstraintType::Unique);
  });
}

const ColumnData* find_column_data(const std::vector<ColumnData>& columns, const Column* column) {
  for (const auto& column_data : columns) {
    if (column_data.column == column) {
      return &column_data;
    }
  }
  return nullptr;
}

ColumnData* find_column_data(std::vector<ColumnData>& columns, const Column* column) {
  for (auto& column_data : columns) {
    if (column_data.column == column) {
      return &column_data;
    }
  }
  return nullptr;
}

std::string value_key(const GeneratedValue& value) {
  std::ostringstream output;
  value.visit([&output](const auto& typed_value) {
    using ValueType = std::decay_t<decltype(typed_value)>;
    output << typeid(ValueType).name() << ':';
    if constexpr (std::is_same_v<ValueType, std::monostate>) {
      output << "null";
    } else if constexpr (std::is_same_v<ValueType, DateValue>) {
      output << typed_value.year << '-' << typed_value.month << '-' << typed_value.day;
    } else if constexpr (std::is_same_v<ValueType, TimeValue>) {
      output << typed_value.hour << ':' << typed_value.minute << ':' << typed_value.second;
    } else if constexpr (std::is_same_v<ValueType, DateTimeValue>) {
      output << typed_value.date.year << '-' << typed_value.date.month << '-' << typed_value.date.day
             << ' ' << typed_value.time.hour << ':' << typed_value.time.minute << ':'
             << typed_value.time.second;
    } else {
      output << typed_value;
    }
  });
  return output.str();
}

std::vector<GeneratedValue> distinct_values(const std::vector<GeneratedValue>& values) {
  std::vector<GeneratedValue> distinct;
  std::unordered_set<std::string> seen;
  for (const auto& value : values) {
    if (seen.insert(value_key(value)).second) {
      distinct.push_back(value);
    }
  }
  return distinct;
}

std::string composite_unique_name(const TableConstraint& constraint) {
  std::string name = "UNIQUE(";
  for (std::size_t index = 0; index < constraint.columns.size(); ++index) {
    if (index > 0) {
      name += ", ";
    }
    name += constraint.columns[index] == nullptr ? "<unknown>"
                                                 : constraint.columns[index]->get_column_name();
  }
  name += ")";
  return name;
}

std::vector<GeneratedValue> composite_domain_for_column(const Table& table,
                                                        const ColumnData& column_data,
                                                        const GenerationConfig& config,
                                                        const KeyRegistry& key_registry) {
  if (const ForeignKey* foreign_key = find_single_column_foreign_key(table, column_data.column);
      foreign_key != nullptr) {
    const int parent_rows = config.get_row_count(foreign_key->referenced_table->get_table_name());
    std::vector<GeneratedValue> values;
    values.reserve(parent_rows);
    for (int row = 0; row < parent_rows; ++row) {
      values.push_back(key_registry
                           .key_at_row(foreign_key->referenced_table,
                                       foreign_key->referenced_columns,
                                       static_cast<std::size_t>(row))
                           .front());
    }
    return distinct_values(values);
  }

  return distinct_values(column_data.data);
}

std::string tuple_key(const std::vector<const ColumnData*>& columns, std::size_t row_index) {
  std::string key;
  for (const ColumnData* column_data : columns) {
    key += value_key(column_data->data[row_index]);
    key += "|";
  }
  return key;
}

void verify_composite_unique_constraints(const TableData& table_data) {
  for (const auto& constraint : table_data.table->get_table_constraints()) {
    if (constraint.type != ConstraintType::Unique || constraint.columns.size() <= 1) {
      continue;
    }

    std::vector<const ColumnData*> columns;
    columns.reserve(constraint.columns.size());
    for (const Column* column : constraint.columns) {
      const ColumnData* column_data = find_column_data(table_data.columns, column);
      if (column_data != nullptr) {
        columns.push_back(column_data);
      }
    }

    if (columns.size() != constraint.columns.size() || columns.empty()) {
      continue;
    }

    const std::size_t row_count = columns.front()->data.size();
    std::unordered_set<std::string> seen;
    for (std::size_t row = 0; row < row_count; ++row) {
      if (!seen.insert(tuple_key(columns, row)).second) {
        throw std::runtime_error("Generated duplicate tuple for composite " +
                                 composite_unique_name(constraint) + " on table '" +
                                 table_data.table->get_table_name() + "'");
      }
    }
  }
}

void apply_composite_unique_constraints(const Table& table, int num_rows,
                                        const GenerationConfig& config,
                                        const KeyRegistry& key_registry,
                                        std::vector<ColumnData>& columns) {
  if (num_rows <= 0) {
    return;
  }

  for (const auto& constraint : table.get_table_constraints()) {
    if (constraint.type != ConstraintType::Unique || constraint.columns.size() <= 1) {
      continue;
    }
    if (has_already_unique_member(table, constraint)) {
      continue;
    }

    std::vector<ColumnData*> constrained_columns;
    std::vector<std::vector<GeneratedValue>> domains;
    constrained_columns.reserve(constraint.columns.size());
    domains.reserve(constraint.columns.size());
    for (const Column* column : constraint.columns) {
      ColumnData* column_data = find_column_data(columns, column);
      if (column_data == nullptr) {
        continue;
      }

      auto domain = composite_domain_for_column(table, *column_data, config, key_registry);
      if (domain.empty()) {
        throw std::runtime_error("Composite " + composite_unique_name(constraint) +
                                 " on table '" + table.get_table_name() +
                                 "' has no values for column '" + column->get_column_name() + "'");
      }

      constrained_columns.push_back(column_data);
      domains.push_back(std::move(domain));
    }

    if (constrained_columns.size() != constraint.columns.size()) {
      continue;
    }

    for (int row = 0; row < num_rows; ++row) {
      std::size_t divisor = 1;
      for (std::size_t column_index = 0; column_index < constrained_columns.size(); ++column_index) {
        divisor = 1;
        for (std::size_t later = column_index + 1; later < domains.size(); ++later) {
          divisor *= domains[later].size();
        }
        const std::size_t value_index =
            (static_cast<std::size_t>(row) / divisor) % domains[column_index].size();
        constrained_columns[column_index]->data[static_cast<std::size_t>(row)] =
            domains[column_index][value_index];
      }
    }
  }
}

}  // namespace

std::vector<ColumnData> GenerationPlan::generate_columns_data(const Table& table, int num_rows,
                                                              const GenerationConfig& config,
                                                              RandomEngine& random_engine,
                                                              const KeyRegistry& key_registry) {
  std::vector<ColumnData> column_data;
  const auto& columns = table.get_columns();
  column_data.reserve(columns.size());
  for (const auto& column_ptr : columns) {
    const Column* column = column_ptr.get();
    column_data.push_back(ColumnData{
        .column = column,
        .data = std::move(ValueGenerator::generate_column_data(
            *column, table, num_rows, config, random_engine, key_registry))});
  }
  apply_composite_unique_constraints(table, num_rows, config, key_registry, column_data);
  return column_data;
}

std::vector<TableData> GenerationPlan::generate_table_data(const std::vector<TablePtr>& tables,
                                                           int num_rows) {
  GenerationConfig config;
  config.default_num_rows = num_rows;
  for (const auto& table_ptr : tables) {
    config.table_row_counts[table_ptr->get_table_name()] = num_rows;
  }

  return generate_table_data(tables, config);
}

std::vector<TableData> GenerationPlan::generate_table_data(const std::vector<TablePtr>& tables,
                                                           const GenerationConfig& config) {
  std::vector<TableData> table_data;
  table_data.reserve(tables.size());
  RandomEngine random_engine(config.seed);
  const KeyRegistry key_registry = KeyRegistry::build_from_tables(tables, config);
  for (const auto& table_ptr : tables) {
    const Table* table = table_ptr.get();
    const int num_rows = config.get_row_count(table->get_table_name());
    if (num_rows < 0) {
      throw std::runtime_error("Row count cannot be negative for table '" + table->get_table_name() +
                               "'");
    }

    table_data.push_back(TableData{
        .table = table,
        .columns = std::move(generate_columns_data(*table, num_rows, config, random_engine,
                                                   key_registry))});
    verify_composite_unique_constraints(table_data.back());
  }
  return table_data;
}

std::vector<TableData> GenerationPlan::generate_table_data(
    const std::vector<TablePtr>& tables, const std::unordered_map<std::string, int>& row_counts,
    int default_num_rows) {
  GenerationConfig config;
  config.default_num_rows = default_num_rows;
  config.table_row_counts = row_counts;
  return generate_table_data(tables, config);
}

std::ostream& operator<<(std::ostream& os, const ColumnData& column_data) {
  os << "ColumnData(column: " << *column_data.column << ", data: [";
  for (const auto& value : column_data.data) {
    os << value << ", ";
  }
  os << "])";
  return os;
}

std::ostream& operator<<(std::ostream& os, const TableData& table_data) {
  os << table_data.table->get_table_name() << '\n';

  for (std::size_t column_index = 0; column_index < table_data.columns.size(); ++column_index) {
    if (column_index > 0) {
      os << " | ";
    }
    os << table_data.columns[column_index].column->get_column_name();
  }
  os << '\n';

  std::size_t row_count = 0;
  if (!table_data.columns.empty()) {
    row_count = table_data.columns.front().data.size();
  }

  for (std::size_t row_index = 0; row_index < row_count; ++row_index) {
    for (std::size_t column_index = 0; column_index < table_data.columns.size(); ++column_index) {
      if (column_index > 0) {
        os << " | ";
      }

      const auto& column_data = table_data.columns[column_index].data;
      if (row_index < column_data.size()) {
        os << column_data[row_index];
      }
    }

    if (row_index + 1 < row_count) {
      os << '\n';
    }
  }

  return os;
}

}  // namespace schemaforge
