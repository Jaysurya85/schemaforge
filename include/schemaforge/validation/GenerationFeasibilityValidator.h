#pragma once
#include <string>
#include <vector>

#include "schemaforge/generator/GenerationConfig.h"
#include "schemaforge/schema/Table.h"
#include "schemaforge/validation/SchemaValidator.h"

namespace schemaforge {

class GenerationFeasibilityValidator {
 private:
  static bool contains_column(const std::vector<std::string>& column_names,
                              const std::string& column_name);
  static bool has_constraint(const Table& table, const Column& column,
                             ConstraintType constraint_type);
  static bool is_supported_generation_type(DataType data_type);
  static bool is_integer_type(DataType data_type);

 public:
  GenerationFeasibilityValidator() = default;
  static ValidationResult validate(const std::vector<Table>& tables,
                                   const GenerationConfig& config);
};

}  // namespace schemaforge
