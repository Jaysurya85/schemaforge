#include "SQLParser.h"
#include <iostream>

int main() {
  std::string sql =
      "CREATE TABLE orders (id INT, user_id INT, amount DECIMAL(10, 2));";

  hsql::SQLParserResult result;
  hsql::SQLParser::parse(sql, &result);
  std::vector<hsql::SQLStatement *> statements = result.getStatements();

  std::cout << "Welcome to Schemaforge" << '\n';

  for (const auto &stmt : statements) {
    if (stmt->isType(hsql::StatementType::kStmtCreate)) {
      std::cout << "Parsed a CREATE statement.\n";
      auto *create_stmt = static_cast<hsql::CreateStatement *>(stmt);
      std::cout << "Table name: " << create_stmt->tableName << '\n';
      std::cout << "Columns are:\n";
      for (const auto &col : *create_stmt->columns) {
        std::cout << "  - " << col->name << " (" << col->type << ")\n";
      }
    }
  }
  return 0;
}
