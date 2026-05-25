#include <iostream>
#include <vector>

#include "schemaforge/generator/GenerationPlan.h"
#include "schemaforge/graph/DependencyGraph.h"
#include "schemaforge/io/FileReader.h"
#include "schemaforge/output/SqlInsertWriter.h"
#include "schemaforge/parser/ParserAdapter.h"
#include "schemaforge/schema/Table.h"
#include "schemaforge/validation/GenerationFeasibilityValidator.h"
#include "schemaforge/validation/SQLiteValidator.h"
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
  if (!validation_result.is_valid) {
    return 1;
  }

  auto dependency_graph = schemaforge::DependencyGraph();
  dependency_graph.make_graph(tables);
  schemaforge::TopologicalTableSortResult table_sort_result =
      dependency_graph.topological_sort_tables(tables);

  if (table_sort_result.has_cycle) {
    std::cerr << "Cycle detected. Tables cannot be fully topologically sorted.\n";
    return 1;
  }

  std::cout << dependency_graph << "\n";
  std::cout << "Topologically sorted tables:\n";
  for (const auto& table : table_sort_result.tables) {
    std::cout << table << "\n\n";
  }

  schemaforge::GenerationConfig generation_config;
  generation_config.default_num_rows = 10;
  generation_config.seed = 42;
  generation_config.table_row_counts = {{"users", 10}, {"orders", 25}};

  schemaforge::ValidationResult generation_validation_result =
      schemaforge::GenerationFeasibilityValidator::validate(table_sort_result.tables,
                                                            generation_config);

  std::cout << "Generation " << generation_validation_result << "\n";
  if (!generation_validation_result.is_valid) {
    return 1;
  }

  std::vector<schemaforge::TableData> table_data =
      schemaforge::GenerationPlan::generate_table_data(table_sort_result.tables, generation_config);

  std::cout << "Generated table data:\n";
  for (const auto& table : table_data) {
    std::cout << table << "\n\n";
  }

  std::vector<std::string> insert_statements =
      schemaforge::SqlInsertWriter::write_inserts(table_data);

  std::cout << "Generated SQL INSERT statements:\n";
  for (const auto& insert_statement : insert_statements) {
    std::cout << insert_statement << '\n';
  }

  schemaforge::ValidationResult sqlite_validation_result =
      schemaforge::SQLiteValidator::validate(sql, insert_statements);

  std::cout << "\nSQLite " << sqlite_validation_result << "\n";
  if (!sqlite_validation_result.is_valid) {
    return 1;
  }

  return 0;
}
