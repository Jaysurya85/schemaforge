#include <iostream>
#include <vector>

#include "schemaforge/generator/GenerationPlan.h"
#include "schemaforge/graph/DependencyGraph.h"
#include "schemaforge/io/FileReader.h"
#include "schemaforge/parser/ParserAdapter.h"
#include "schemaforge/schema/Table.h"
#include "schemaforge/validation/SchemaValidator.h"

auto main() -> int {
  std::string sql = schemaforge::FileReader::read_file("schema.sql");

  std::cout << "Welcome to Schemaforge" << '\n';
  std::cout << "Parsing SQL:\n" << sql << "\n\n";

  std::vector<schemaforge::Table> tables = schemaforge::ParserAdapter::parse(sql);
  schemaforge::ValidationResult validation_result = schemaforge::SchemaValidator::validate(tables);

  std::cout << tables.size() << " tables parsed.\n";
  for (const auto& table : tables) {
    std::cout << table << "\n\n";
  }

  std::cout << validation_result << "\n";

  auto dependency_graph = schemaforge::DependencyGraph();
  dependency_graph.make_graph(tables);
  schemaforge::TopologicalTableSortResult table_sort_result =
      schemaforge::DependencyGraph::sort_tables(tables);

  if (table_sort_result.has_cycle) {
    std::cerr << "Cycle detected. Tables cannot be fully topologically sorted.\n";
    return 1;
  }

  std::cout << dependency_graph << "\n";
  std::cout << "Topologically sorted tables:\n";
  for (const auto& table : table_sort_result.tables) {
    std::cout << table << "\n\n";
  }

  std::vector<schemaforge::TableData> table_data =
      schemaforge::GenerationPlan::generate_table_data(tables, 10);

  std::cout << "Generated table data:\n";
  for (const auto& table : table_data) {
    std::cout << table << "\n\n";
  }

  return 0;
}
