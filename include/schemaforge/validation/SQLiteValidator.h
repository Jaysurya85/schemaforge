#pragma once
#include <string>
#include <vector>

#include "schemaforge/validation/ValidationResult.h"

namespace schemaforge {

class SQLiteValidator {
 private:
  static bool execute_sql(void* database, const std::string& sql, std::string& error);

 public:
  SQLiteValidator() = default;
  static ValidationResult validate(const std::string& schema_sql,
                                   const std::vector<std::string>& insert_statements);
  static ValidationResult validate_file(const std::string& schema_sql,
                                        const std::string& insert_file);
};

}  // namespace schemaforge
