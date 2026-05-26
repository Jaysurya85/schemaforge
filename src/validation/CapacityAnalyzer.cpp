#include "schemaforge/validation/CapacityAnalyzer.h"

#include <algorithm>

namespace schemaforge {

const TableCapacityInfo* SchemaCapacityInfo::find_table(const std::string& table_name) const {
  const auto table_info =
      std::ranges::find_if(tables, [&table_name](const TableCapacityInfo& candidate) {
        return candidate.table_name == table_name;
      });

  if (table_info == tables.end()) {
    return nullptr;
  }

  return &*table_info;
}

bool CapacityAnalyzer::contains_column(const std::vector<std::string>& column_names,
                                       const std::string& column_name) {
  return std::ranges::find(column_names, column_name) != column_names.end();
}

bool CapacityAnalyzer::has_constraint(const Table& table, const Column& column,
                                      ConstraintType constraint_type) {
  const auto column_name = column.get_column_name();
  for (const auto& constraint : table.get_table_constraints()) {
    if (constraint.type == constraint_type &&
        contains_column(constraint.columnNames, column_name)) {
      return true;
    }
  }
  return false;
}

void CapacityAnalyzer::apply_capacity_limit(TableCapacityInfo& table_info,
                                            const RowCapacityLimit& capacity_limit) {
  table_info.reasons.push_back(capacity_limit.reason);

  if (!table_info.static_max_rows.has_value() ||
      capacity_limit.max_rows < table_info.static_max_rows->max_rows) {
    table_info.static_max_rows = capacity_limit;
  }
}

SchemaCapacityInfo CapacityAnalyzer::analyze(const std::vector<Table>& tables) {
  SchemaCapacityInfo capacity_info;
  capacity_info.tables.reserve(tables.size());

  for (const auto& table : tables) {
    TableCapacityInfo table_info{.table_name = table.get_table_name(),
                                 .static_max_rows = std::nullopt,
                                 .reasons = {}};

    for (const auto& column : table.get_columns()) {
      if (has_constraint(table, column, ConstraintType::Unique) &&
          column.get_column_type().data_type == DataType::BOOLEAN) {
        apply_capacity_limit(
            table_info,
            RowCapacityLimit{
                .max_rows = 2,
                .reason = "UNIQUE BOOLEAN column '" + table.get_table_name() + "." +
                          column.get_column_name() + "' because BOOLEAN has only 2 possible values"});
      }
    }

    capacity_info.tables.push_back(std::move(table_info));
  }

  return capacity_info;
}

}  // namespace schemaforge
