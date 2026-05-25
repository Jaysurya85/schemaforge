#pragma once
#include <string>
#include <vector>

#include "schemaforge/validation/SchemaValidator.h"

namespace schemaforge {

class SQLiteValidator {
 private:
  static bool execute_sql(void* database, const std::string& sql, std::string& error);

 public:
  SQLiteValidator() = default;
  static ValidationResult validate(const std::string& schema_sql,
                                   const std::vector<std::string>& insert_statements);
};

}  // namespace schemaforge
