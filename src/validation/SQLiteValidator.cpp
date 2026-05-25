#include "schemaforge/validation/SQLiteValidator.h"

#include <sqlite3.h>

namespace schemaforge {

bool SQLiteValidator::execute_sql(void* database, const std::string& sql, std::string& error) {
  char* sqlite_error = nullptr;
  const int result =
      sqlite3_exec(static_cast<sqlite3*>(database), sql.c_str(), nullptr, nullptr, &sqlite_error);

  if (result == SQLITE_OK) {
    return true;
  }

  if (sqlite_error != nullptr) {
    error = sqlite_error;
    sqlite3_free(sqlite_error);
  } else {
    error = sqlite3_errmsg(static_cast<sqlite3*>(database));
  }

  return false;
}

ValidationResult SQLiteValidator::validate(const std::string& schema_sql,
                                           const std::vector<std::string>& insert_statements) {
  sqlite3* database = nullptr;
  if (sqlite3_open(":memory:", &database) != SQLITE_OK) {
    std::string error = "Could not open in-memory SQLite database";
    if (database != nullptr) {
      error += ": ";
      error += sqlite3_errmsg(database);
      sqlite3_close(database);
    }
    return {false, {error}};
  }

  ValidationResult validation_result(true, {});
  auto execute_checked = [&validation_result, database](const std::string& sql,
                                                        const std::string& context) {
    std::string error;
    if (!execute_sql(database, sql, error)) {
      validation_result.is_valid = false;
      validation_result.errors.push_back(context + ": " + error);
    }
  };

  execute_checked("PRAGMA foreign_keys = ON;", "Failed to enable SQLite foreign key checks");
  execute_checked(schema_sql, "Failed to execute schema SQL");

  if (validation_result.is_valid) {
    execute_checked("BEGIN TRANSACTION;", "Failed to begin SQLite validation transaction");
    for (const auto& insert_statement : insert_statements) {
      execute_checked(insert_statement, "Failed SQL statement [" + insert_statement + "]");
    }

    if (validation_result.is_valid) {
      execute_checked("COMMIT;", "Failed to commit SQLite validation transaction");
    } else {
      std::string rollback_error;
      execute_sql(database, "ROLLBACK;", rollback_error);
    }
  }

  sqlite3_close(database);
  return validation_result;
}

}  // namespace schemaforge
