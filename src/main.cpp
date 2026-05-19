#include "schemaforge/parser/ParserAdapter.h"
#include "schemaforge/schema/Table.h"
#include <iostream>
#include <vector>

int main() {
  std::string sql = "CREATE TABLE users (id INT PRIMARY KEY, name TEXT NOT "
                    "NULL,  email TEXT UNIQUE);";

  sql += "CREATE TABLE orders(id INT PRIMARY KEY, user_id INT NOT NULL, amount "
         "DECIMAL, FOREIGN KEY(user_id) REFERENCES users(id))";

  std::cout << "Welcome to Schemaforge" << '\n';
  std::cout << "Parsing SQL:\n" << sql << "\n\n";
  schemaforge::ParserAdapter parser_adapter;
  std::vector<schemaforge::Table> tables = parser_adapter.parse(sql);

  std::cout << tables.size() << " tables parsed.\n";
  for (const auto &table : tables) {
    std::cout << table << "\n\n";
  }
  return 0;
}
