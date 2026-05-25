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
  static std::pair<bool, std::string> check_foreign_keys(
      const std::vector<ForeignKey>& foreign_keys, const std::vector<Table>& tables);

 public:
  SchemaValidator() = default;
  static ValidationResult validate(const std::vector<Table>& tables);
};

std::ostream& operator<<(std::ostream& os, const ValidationResult& validation_result);
}  // namespace schemaforge
