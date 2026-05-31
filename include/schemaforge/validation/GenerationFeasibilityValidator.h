#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "schemaforge/generator/GenerationConfig.h"
#include "schemaforge/schema/Table.h"
#include "schemaforge/validation/CapacityAnalyzer.h"
#include "schemaforge/validation/SchemaValidator.h"

namespace schemaforge {

class GenerationFeasibilityValidator {
 private:
  std::unordered_map<TableId, TableCapacityInfo> table_capacity_info;

  void load_capacity_info(const SchemaCapacityInfo& capacity_info);
  bool contains_column(const std::vector<Column*>& columns, const Column* column);
  bool has_matching_constraint(const Table* table, const std::vector<Column*>& columns,
                               ConstraintType constraint_type);
  bool has_unique_key_constraint(const Table* table, const std::vector<Column*>& columns);
  void apply_capacity_limit(ValidationResult& validation_result, TableCapacityInfo& table_info,
                            int max_rows, const std::string& reason);
  void validate_table_capacity(ValidationResult& validation_result,
                               const TableCapacityInfo& table_info) const;
  void apply_foreign_key_capacity(ValidationResult& validation_result, Table* table);

 public:
  explicit GenerationFeasibilityValidator(const SchemaCapacityInfo& capacity_info);
  ValidationResult validate(const std::vector<TablePtr>& tables, const GenerationConfig& config,
                            const SchemaCapacityInfo& capacity_info);
  ValidationResult validate(const std::vector<TablePtr>& tables, const GenerationConfig& config);
};

}  // namespace schemaforge
