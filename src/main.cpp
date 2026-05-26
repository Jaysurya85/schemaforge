#include <exception>
#include <iostream>
#include <string>
#include <vector>

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

namespace {

schemaforge::GenerationConfig make_default_generation_config() {
  schemaforge::GenerationConfig generation_config;
  generation_config.default_num_rows = 10;
  generation_config.seed = 42;
  generation_config.table_row_counts = {{"users", 10}, {"orders", 25}};
  return generation_config;
}

auto apply_generation_option(schemaforge::GenerationConfig& generation_config,
                             const std::string& option, const std::string& value) -> bool {
  try {
    if (option == "--default-rows") {
      generation_config.default_num_rows = std::stoi(value);
      return true;
    }

    if (option == "--seed") {
      generation_config.seed = static_cast<unsigned int>(std::stoul(value));
      return true;
    }

    if (option == "--rows") {
      const std::size_t separator = value.find('=');
      if (separator == std::string::npos || separator == 0 || separator + 1 >= value.size()) {
        std::cerr << "Expected --rows table=count, got: " << value << '\n';
        return false;
      }

      const std::string table_name = value.substr(0, separator);
      const int row_count = std::stoi(value.substr(separator + 1));
      generation_config.table_row_counts[table_name] = row_count;
      return true;
    }
  } catch (const std::exception& error) {
    std::cerr << "Invalid value for " << option << ": " << value << " (" << error.what()
              << ")\n";
    return false;
  }

  return false;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
  std::string schema_path = "schema.sql";
  schemaforge::GenerationConfig generation_config = make_default_generation_config();

  for (int arg_index = 1; arg_index < argc; ++arg_index) {
    const std::string argument = argv[arg_index];
    if (argument == "--default-rows" || argument == "--seed" || argument == "--rows") {
      if (arg_index + 1 >= argc) {
        std::cerr << "Missing value for " << argument << '\n';
        return 1;
      }

      if (!apply_generation_option(generation_config, argument, argv[++arg_index])) {
        return 1;
      }
      continue;
    }

    if (!argument.empty() && argument.starts_with("--")) {
      std::cerr << "Unknown option: " << argument << '\n';
      return 1;
    }

    schema_path = argument;
  }

  std::string sql = schemaforge::FileReader::read_file(schema_path);

  std::cout << "Welcome to Schemaforge" << '\n';
  std::cout << "Schema file: " << schema_path << '\n';
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

  schemaforge::SchemaCapacityInfo capacity_info =
      schemaforge::CapacityAnalyzer::analyze(table_sort_result.tables);

  schemaforge::ValidationResult generation_validation_result =
      schemaforge::GenerationFeasibilityValidator::validate(table_sort_result.tables,
                                                            generation_config, capacity_info);

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
