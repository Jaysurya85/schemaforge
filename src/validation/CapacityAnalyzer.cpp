#include "schemaforge/validation/CapacityAnalyzer.h"

#include <algorithm>
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

std::optional<RowCapacityLimit> CapacityAnalyzer::column_capacity_limit(const Table* table,
                                                                        const Column* column) {
  if (has_constraint(table, column, ConstraintType::Unique) &&
      column->get_column_type().data_type == DataType::BOOLEAN) {
    return RowCapacityLimit{.max_rows = 2,
                            .reason = "UNIQUE BOOLEAN column '" + table->get_table_name() + "." +
                                      column->get_column_name() +
                                      "' because BOOLEAN has only 2 possible values"};
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
