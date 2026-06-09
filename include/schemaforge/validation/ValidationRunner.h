#pragma once

#include <string>
#include <vector>

#include "schemaforge/config/GenerationConfig.h"
#include "schemaforge/schema/Table.h"
#include "schemaforge/validation/CapacityAnalyzer.h"
#include "schemaforge/validation/ValidationResult.h"

namespace schemaforge {

class ValidationRunner {
 public:
  ValidationRunner() = default;

  static ValidationResult validate_schema_file(const std::string& schema_path);
  static ValidationResult validate_schema(const std::vector<TablePtr>& tables);
  static ValidationResult validate_config(const std::vector<TablePtr>& tables,
                                          const GenerationConfig& config);
  static ValidationResult validate_generation(const std::vector<TablePtr>& tables,
                                              const GenerationConfig& config,
                                              const SchemaCapacityInfo& capacity_info);
  static ValidationResult validate_sqlite(const std::string& schema_sql,
                                          const std::vector<std::string>& insert_statements);
};

}  // namespace schemaforge
