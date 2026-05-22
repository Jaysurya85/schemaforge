#pragma once
#include "schemaforge/schema/Table.h"
#include <string>
#include <vector>

namespace schemaforge {
struct ValidationResult {
  bool isValid{true};
  std::vector<std::string> errors{};
};

class SchemaValidator {
private:
  std::pair<bool, std::string>
  check_foreign_keys(const std::vector<ForeignKey> &foreign_keys,
                     const std::vector<Table> &tables);

public:
  SchemaValidator() = default;
  ValidationResult validate(const std::vector<Table> &tables);
};

std::ostream &operator<<(std::ostream &os,
                         const ValidationResult &validation_result);
} // namespace schemaforge
