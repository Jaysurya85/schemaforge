#pragma once

#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace schemaforge {

struct ValidationResult {
  ValidationResult(bool is_valid, std::vector<std::string> errors)
      : is_valid(is_valid), errors(std::move(errors)) {}

  bool is_valid;
  std::vector<std::string> errors;
};

std::ostream& operator<<(std::ostream& os, const ValidationResult& validation_result);

}  // namespace schemaforge
