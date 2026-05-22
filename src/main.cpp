#include "schemaforge/graph/DependencyGraph.h"
#include "schemaforge/io/FileReader.h"
#include "schemaforge/parser/ParserAdapter.h"
#include "schemaforge/schema/Table.h"
#include "schemaforge/validation/SchemaValidator.h"
#include <iostream>
#include <vector>

int main() {
  std::string sql = schemaforge::FileReader::read_file("schema.sql");

  std::cout << "Welcome to Schemaforge" << '\n';
  std::cout << "Parsing SQL:\n" << sql << "\n\n";
  schemaforge::ParserAdapter parser_adapter;

  std::vector<schemaforge::Table> tables = parser_adapter.parse(sql);
  schemaforge::ValidationResult validation_result =
      schemaforge::SchemaValidator().validate(tables);

  std::cout << tables.size() << " tables parsed.\n";
  for (const auto &table : tables) {
    std::cout << table << "\n\n";
  }

  std::cout << validation_result << "\n";

  auto dependency_graph = schemaforge::DependencyGraph();
  dependency_graph.make_graph(tables);
  schemaforge::TopologicalSortResult sort_result =
      dependency_graph.topological_sort();

  std::cout << dependency_graph << "\n";
  for (const auto &table_name : sort_result.order) {
    std::cout << table_name << "\n";
  }

  if (sort_result.has_cycle) {
    std::cout << "Cycle detected. Remaining tables:\n";
  }

  return 0;
}
