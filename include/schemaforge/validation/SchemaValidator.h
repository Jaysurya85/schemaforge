#pragma once
#include <string>
#include <utility>
#include <vector>

#include "schemaforge/schema/Table.h"

namespace schemaforge {
struct ValidationResult {
  ValidationResult(bool is_valid, std::vector<std::string> errors)
      : is_valid(is_valid), errors(std::move(errors)) {}
  bool is_valid;
  std::vector<std::string> errors;
};

class SchemaValidator {
 private:
  static bool is_supported_generation_type(DataType data_type);
  static bool is_integer_type(DataType data_type);
  static const Table* find_table(const std::vector<TablePtr>& tables, const std::string& table_name);
  static const Column* find_column(const Table* table, const std::string& column_name);
  static const Column* primary_key_column(const Table* table);
  static void check_supported_generation_types(ValidationResult& validation_result,
                                               const Table& table);
  static std::pair<bool, std::string> check_foreign_keys_specs(
      const std::vector<ForeignKeySpec>& foreign_keys_spec, const std::vector<TablePtr>& tables);

 public:
  SchemaValidator() = default;
  static ValidationResult validate(const std::vector<TablePtr>& tables);
};

std::ostream& operator<<(std::ostream& os, const ValidationResult& validation_result);
}  // namespace schemaforge
