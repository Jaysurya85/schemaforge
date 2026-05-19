#include "schemaforge/parser/ParserAdapter.h"
#include "schemaforge/schema/Table.h"
#include <iostream>
#include <vector>

int main() {
  std::string sql =
      "CREATE TABLE orders (id INT, user_id INT, amount DECIMAL(10, 2));";
  sql += "CREATE TABLE users (id INT, name VARCHAR(255));";

  std::cout << "Welcome to Schemaforge" << '\n';
  schemaforge::ParserAdapter parser_adapter;
  std::vector<schemaforge::Table> tables = parser_adapter.parse(sql);

  std::cout << tables.size() << " tables parsed.\n";
  std::cout << parser_adapter.print(tables) << '\n';
  return 0;
}
