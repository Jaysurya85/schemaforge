#include "schemaforge/parser/ParserAdapter.h"
#include "schemaforge/schema/Table.h"
#include "schemaforge/validation/SchemaValidator.h"
#include <iostream>
#include <vector>

int main() {

  std::string sql1 = "CREATE TABLE users (id INT PRIMARY KEY, email TEXT "
                     "UNIQUE, name TEXT NOT NULL);";

  sql1 += "CREATE TABLE orders(id INT PRIMARY KEY, user_id INT NOT NULL, "
          "amount DECIMAL, FOREIGN KEY(user_id) REFERENCES users(ida));";

  std::cout << "Welcome to Schemaforge" << '\n';
  std::cout << "Parsing SQL:\n" << sql1 << "\n\n";
  schemaforge::ParserAdapter parser_adapter;
  try {

    std::vector<schemaforge::Table> tables = parser_adapter.parse(sql1);
    schemaforge::ValidationResult validation_result =
        schemaforge::SchemaValidator().validate(tables);

    std::cout << tables.size() << " tables parsed.\n";
    for (const auto &table : tables) {
      std::cout << table << "\n\n";
    }

    std::cout << validation_result << "\n";
  } catch (const std::exception &e) {
    std::cerr << "Error parsing SQL: " << e.what() << "\n";
    return 1;
  }
  return 0;
}
