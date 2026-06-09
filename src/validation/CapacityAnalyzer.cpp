#include "schemaforge/validation/CapacityAnalyzer.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace schemaforge {

bool CapacityAnalyzer::contains_column(const std::vector<Column*>& columns, const Column* column) {
  return std::ranges::find(columns, column) != columns.end();
}

bool CapacityAnalyzer::has_constraint(const Table* table, const Column* column,
                                      ConstraintType constraint_type) {
  for (const auto& constraint : table->get_table_constraints()) {
    if (constraint.type == constraint_type && contains_column(constraint.columns, column)) {
      return true;
    }
  }
  return false;
}

int char_length(const Column* column) {
  const int64_t parsed_length = column->get_column_type().length;
  if (parsed_length <= 0) {
    return 1;
  }
  return static_cast<int>(parsed_length);
}

int char_capacity(int length) {
  int capacity = 1;
  for (int index = 0; index < length; ++index) {
    if (capacity > std::numeric_limits<int>::max() / 26) {
      return std::numeric_limits<int>::max();
    }
    capacity *= 26;
  }
  return capacity;
}

std::optional<RowCapacityLimit> CapacityAnalyzer::column_capacity_limit(const Table* table,
                                                                        const Column* column) {
  if (has_constraint(table, column, ConstraintType::Unique) &&
      column->get_column_type().data_type == DataType::BOOLEAN) {
    return RowCapacityLimit{.max_rows = 2,
                            .reason = "UNIQUE BOOLEAN column '" + table->get_table_name() + "." +
                                      column->get_column_name() +
                                      "' because BOOLEAN has only 2 possible values"};
  }

  if (column->get_column_type().data_type == DataType::CHAR) {
    const bool is_unique = has_constraint(table, column, ConstraintType::Unique);
    const bool is_primary_key = has_constraint(table, column, ConstraintType::PrimaryKey);
    if (is_unique || is_primary_key) {
      const int length = char_length(column);
      const int capacity = char_capacity(length);
      const std::string qualified_column_name =
          table->get_table_name() + "." + column->get_column_name();
      const std::string constraint_name = is_primary_key ? "PRIMARY KEY" : "UNIQUE";
      return RowCapacityLimit{.max_rows = capacity,
                              .reason = "Column " + qualified_column_name + " is " +
                                        constraint_name + " CHAR(" + std::to_string(length) +
                                        ") and can only produce " + std::to_string(capacity) +
                                        " distinct values."};
    }
  }

  return std::nullopt;
}

void CapacityAnalyzer::apply_capacity_limit(TableCapacityInfo& table_info,
                                            const RowCapacityLimit& capacity_limit) {
  table_info.reasons.push_back(capacity_limit.reason);
  table_info.max_rows = std::min(table_info.max_rows, capacity_limit.max_rows);
}

SchemaCapacityInfo CapacityAnalyzer::analyze(const std::vector<TablePtr>& tables,
                                             const GenerationConfig& config) {
  SchemaCapacityInfo capacity_info;
  capacity_info.tables.reserve(tables.size());

  for (const auto& table_ptr : tables) {
    const TableId table_id = table_ptr->get_table_name();
    TableCapacityInfo table_info{.table = table_ptr.get(),
                                 .table_id = table_id,
                                 .requested_rows = config.get_row_count(table_id),
                                 .max_rows = std::numeric_limits<int>::max(),
                                 .reasons = {}};

    apply_capacity_limit(table_info,
                         RowCapacityLimit{.max_rows = table_info.requested_rows,
                                          .reason = "requested row count for table '" + table_id +
                                                    "'"});

    for (const auto& column_ptr : table_ptr->get_columns()) {
      auto column_limit = column_capacity_limit(table_ptr.get(), column_ptr.get());
      if (column_limit.has_value()) {
        apply_capacity_limit(table_info, column_limit.value());
      }
    }

    capacity_info.tables.push_back(std::move(table_info));
  }

  return capacity_info;
}

}  // namespace schemaforge
