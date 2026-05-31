#include <iostream>
#include <string>
#include <vector>

#include "schemaforge/generator/GenerationConfig.h"
#include "schemaforge/generator/GenerationPlan.h"
#include "schemaforge/graph/DependencyGraph.h"
#include "schemaforge/io/FileReader.h"
#include "schemaforge/output/SqlInsertWriter.h"
#include "schemaforge/parser/ParserAdapter.h"
#include "schemaforge/schema/Table.h"
#include "schemaforge/validation/CapacityAnalyzer.h"
#include "schemaforge/validation/GenerationFeasibilityValidator.h"
#include "schemaforge/validation/SQLiteValidator.h"
#include "schemaforge/validation/SchemaValidator.h"

auto main(int argc, char* argv[]) -> int {
  std::string schema_path = "schema.sql";
  schemaforge::GenerationConfig generation_config = schemaforge::GenerationConfig::make_default();

  if (!generation_config.apply_cli_args(argc, argv, schema_path)) {
    return 1;
  }

  std::string sql = schemaforge::FileReader::read_file(schema_path);

  std::cout << "Welcome to Schemaforge" << '\n';
  std::cout << "Schema file: " << schema_path << '\n';
  std::cout << "Parsing SQL:\n" << sql << "\n\n";

  std::vector<schemaforge::TablePtr> tables = schemaforge::ParserAdapter::parse(sql);
  schemaforge::ValidationResult validation_result = schemaforge::SchemaValidator::validate(tables);

  std::cout << validation_result << "\n";
  if (!validation_result.is_valid) {
    return 1;
  }

  schemaforge::ParserAdapter::foreign_key_resolver(tables);

  std::cout << tables.size() << " tables parsed.\n";
  for (const auto& table : tables) {
    std::cout << *table << "\n\n";
  }

  auto dependency_graph = schemaforge::DependencyGraph();
  dependency_graph.make_graph(tables);
  schemaforge::TopologicalTableSortResult table_sort_result =
      dependency_graph.topological_sort_tables(tables);

  if (table_sort_result.has_cycle) {
    std::cerr << "Cycle detected. Tables cannot be fully topologically sorted.\n";
    return 1;
  }

  std::vector<schemaforge::TablePtr> sorted_tables =
      schemaforge::DependencyGraph::get_sorted_tables(std::move(tables), table_sort_result.order);

  std::cout << dependency_graph << "\n";

  std::cout << "Topologically sorted tables:\n";
  for (const auto& table_ptr : sorted_tables) {
    std::cout << table_ptr->get_table_name() << "\n";
  }

  generation_config.write_context_file(table_sort_result.order);

  schemaforge::SchemaCapacityInfo capacity_info =
      schemaforge::CapacityAnalyzer::analyze(sorted_tables, generation_config);

  schemaforge::GenerationFeasibilityValidator validator(capacity_info);
  schemaforge::ValidationResult generation_validation_result =
      validator.validate(sorted_tables, generation_config, capacity_info);

  std::cout << "Generation " << generation_validation_result << "\n";
  if (!generation_validation_result.is_valid) {
    return 1;
  }

  std::vector<schemaforge::TableData> table_data =
      schemaforge::GenerationPlan::generate_table_data(sorted_tables, generation_config);

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
