#pragma once

#include <string>
#include <vector>

#include "schemaforge/config/GenerationConfig.h"
#include "schemaforge/schema/Table.h"

namespace schemaforge {

enum class PostgresDockerValidationStatus {
  Failed,
  Passed,
  Unavailable,
};

struct PostgresDockerValidationResult {
  PostgresDockerValidationStatus status;
  std::vector<std::string> errors;
};

class PostgresDockerValidator {
 public:
  static PostgresDockerValidationResult validate(const std::string& schema_path,
                                                 const std::vector<TablePtr>& tables,
                                                 const GenerationConfig& config);
};

}  // namespace schemaforge
