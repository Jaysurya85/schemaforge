#include "schemaforge/validation/GenerationFeasibilityValidator.h"

#include <algorithm>

namespace schemaforge {

GenerationFeasibilityValidator::GenerationFeasibilityValidator(
    const SchemaCapacityInfo& capacity_info) {
  load_capacity_info(capacity_info);
}

void GenerationFeasibilityValidator::load_capacity_info(const SchemaCapacityInfo& capacity_info) {
  table_capacity_info.clear();
  table_capacity_info.reserve(capacity_info.tables.size());
  for (const auto& table_info : capacity_info.tables) {
    table_capacity_info.emplace(table_info.table_id, table_info);
  }
}

bool GenerationFeasibilityValidator::contains_column(const std::vector<Column*>& columns,
                                                     const Column* column) {
  return std::ranges::find(columns, column) != columns.end();
}

bool GenerationFeasibilityValidator::has_matching_constraint(const Table* table,
                                                             const std::vector<Column*>& columns,
                                                             ConstraintType constraint_type) {
  for (const auto& constraint : table->get_table_constraints()) {
    if (constraint.type != constraint_type || constraint.columns.size() != columns.size()) {
      continue;
    }

    const bool all_columns_match =
        std::ranges::all_of(columns, [&constraint, this](const Column* column) {
          return contains_column(constraint.columns, column);
        });
    if (all_columns_match) {
      return true;
    }
  }

  return false;
}

bool GenerationFeasibilityValidator::has_unique_key_constraint(
    const Table* table, const std::vector<Column*>& columns) {
  return has_matching_constraint(table, columns, ConstraintType::Unique) ||
         has_matching_constraint(table, columns, ConstraintType::PrimaryKey);
}

void GenerationFeasibilityValidator::apply_capacity_limit(ValidationResult& validation_result,
                                                          TableCapacityInfo& table_info,
                                                          int max_rows, const std::string& reason) {
  if (max_rows >= table_info.max_rows) {
    return;
  }

  table_info.max_rows = max_rows;
  table_info.reasons.push_back(reason);
  validate_table_capacity(validation_result, table_info);
}

void GenerationFeasibilityValidator::validate_table_capacity(
    ValidationResult& validation_result, const TableCapacityInfo& table_info) const {
  if (table_info.requested_rows <= table_info.max_rows) {
    return;
  }

  validation_result.is_valid = false;
  std::string error = "Cannot generate " + std::to_string(table_info.requested_rows) +
                      " rows for table '" + table_info.table_id + "': max rows is " +
                      std::to_string(table_info.max_rows);
  if (!table_info.reasons.empty()) {
    error += " because " + table_info.reasons.back();
  }
  validation_result.errors.push_back(std::move(error));
}

void GenerationFeasibilityValidator::apply_foreign_key_capacity(ValidationResult& validation_result,
                                                                Table* table) {
  auto table_info_it = table_capacity_info.find(table->get_table_name());
  if (table_info_it == table_capacity_info.end()) {
    return;
  }

  TableCapacityInfo& table_info = table_info_it->second;
  for (const auto& foreign_key : table->get_foreign_keys()) {
    const Table* parent_table = foreign_key.referenced_table;
    const auto parent_info_it = table_capacity_info.find(parent_table->get_table_name());
    if (parent_info_it == table_capacity_info.end()) {
      continue;
    }

    const TableCapacityInfo& parent_info = parent_info_it->second;
    if (table_info.requested_rows > 0 && parent_info.max_rows <= 0) {
      validation_result.is_valid = false;
      validation_result.errors.push_back("Cannot generate " + table_info.table_id +
                                         ": it references table '" + parent_info.table_id +
                                         "', but that table has 0 rows");
      continue;
    }

    if (!has_unique_key_constraint(table, foreign_key.local_columns)) {
      continue;
    }

    const int referenced_key_capacity = parent_info.max_rows;
    const std::string reason = "UNIQUE foreign key on table '" + table_info.table_id +
                               "' references table '" + parent_info.table_id + "' with " +
                               std::to_string(referenced_key_capacity) + " available rows";

    apply_capacity_limit(validation_result, table_info, referenced_key_capacity, reason);
  }
}

ValidationResult GenerationFeasibilityValidator::validate(const std::vector<TablePtr>& tables,
                                                          const GenerationConfig&,
                                                          const SchemaCapacityInfo& capacity_info) {
  ValidationResult validation_result(true, {});
  load_capacity_info(capacity_info);

  for (const auto& table_ptr : tables) {
    auto table_info_it = table_capacity_info.find(table_ptr->get_table_name());
    if (table_info_it != table_capacity_info.end()) {
      validate_table_capacity(validation_result, table_info_it->second);
    }

    apply_foreign_key_capacity(validation_result, table_ptr.get());
  }

  return validation_result;
}

ValidationResult GenerationFeasibilityValidator::validate(const std::vector<TablePtr>& tables,
                                                          const GenerationConfig& config) {
  const SchemaCapacityInfo capacity_info = CapacityAnalyzer::analyze(tables, config);
  return validate(tables, config, capacity_info);
}

}  // namespace schemaforge
