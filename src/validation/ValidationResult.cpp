#include "schemaforge/validation/ValidationResult.h"

namespace schemaforge {

std::ostream& operator<<(std::ostream& os, const ValidationResult& validation_result) {
  os << "Validation Result: " << (validation_result.is_valid ? "Valid" : "Invalid") << "\n";
  if (!validation_result.is_valid) {
    os << "Errors:\n";
    for (const auto& error : validation_result.errors) {
      os << "- " << error << "\n";
    }
  }
  return os;
}

}  // namespace schemaforge
