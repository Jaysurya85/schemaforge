#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "schemaforge/config/GenerationConfig.h"
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

struct SchemaAnalysis {
  std::string sql;
  std::vector<schemaforge::TablePtr> sorted_tables;
  std::vector<schemaforge::TableId> table_order;
};

void print_usage() {
  std::cerr << "Usage:\n"
            << "  schemaforge init [--schema schema.sql] [--config schemaforge.yaml] "
               "[--seed 42] [--default-rows 10]\n"
            << "  schemaforge generate [--config schemaforge.yaml]\n";
}

SchemaAnalysis analyze_schema(const std::string& schema_path) {
  std::string sql = schemaforge::FileReader::read_file(schema_path);

  std::cout << "Welcome to Schemaforge" << '\n';
  std::cout << "Schema file: " << schema_path << '\n';
  std::cout << "Parsing SQL:\n" << sql << "\n\n";

  std::vector<schemaforge::TablePtr> tables = schemaforge::ParserAdapter::parse(sql);
  schemaforge::ValidationResult validation_result = schemaforge::SchemaValidator::validate(tables);

  std::cout << validation_result << "\n";
  if (!validation_result.is_valid) {
    return {.sql = std::move(sql), .sorted_tables = {}, .table_order = {}};
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
    return {.sql = std::move(sql), .sorted_tables = {}, .table_order = {}};
  }

  std::vector<schemaforge::TableId> table_order = table_sort_result.order;
  std::vector<schemaforge::TablePtr> sorted_tables =
      schemaforge::DependencyGraph::get_sorted_tables(std::move(tables), table_sort_result.order);

  std::cout << dependency_graph << "\n";

  std::cout << "Topologically sorted tables:\n";
  for (const auto& table_ptr : sorted_tables) {
    std::cout << table_ptr->get_table_name() << "\n";
  }

  return {.sql = std::move(sql),
          .sorted_tables = std::move(sorted_tables),
          .table_order = std::move(table_order)};
}

bool write_output_file(const std::string& output_file,
                       const std::vector<std::string>& insert_statements) {
  std::ofstream file(output_file);
  if (!file.is_open()) {
    std::cerr << "Failed to open output file: " << output_file << '\n';
    return false;
  }

  for (const auto& insert_statement : insert_statements) {
    file << insert_statement << '\n';
  }

  return true;
}

int run_init(int argc, char* argv[]) {
  std::string config_path = "schemaforge.yaml";
  schemaforge::GenerationConfig generation_config = schemaforge::GenerationConfig::make_default();

  if (!generation_config.apply_init_args(argc, argv, 2, config_path)) {
    return 1;
  }

  SchemaAnalysis analysis = analyze_schema(generation_config.schema_path);
  if (analysis.sorted_tables.empty() && !analysis.table_order.empty()) {
    return 1;
  }
  if (analysis.table_order.empty()) {
    return 1;
  }

  generation_config.write_context_file(analysis.table_order, config_path);
  std::cout << "Created config file: " << config_path << '\n';
  return 0;
}

int run_generate(int argc, char* argv[]) {
  std::string config_path = "schemaforge.yaml";
  schemaforge::GenerationConfig generation_config = schemaforge::GenerationConfig::make_default();

  if (!generation_config.apply_generate_args(argc, argv, 2, config_path)) {
    return 1;
  }

  if (!generation_config.read_context_file(config_path)) {
    return 1;
  }

  if (generation_config.output_format != "sql") {
    std::cerr << "Unsupported output format: " << generation_config.output_format << '\n';
    return 1;
  }

  SchemaAnalysis analysis = analyze_schema(generation_config.schema_path);
  if (analysis.table_order.empty()) {
    return 1;
  }

  schemaforge::SchemaCapacityInfo capacity_info =
      schemaforge::CapacityAnalyzer::analyze(analysis.sorted_tables, generation_config);

  schemaforge::GenerationFeasibilityValidator validator(capacity_info);
  schemaforge::ValidationResult generation_validation_result =
      validator.validate(analysis.sorted_tables, generation_config, capacity_info);

  std::cout << "Generation " << generation_validation_result << "\n";
  if (!generation_validation_result.is_valid) {
    return 1;
  }

  std::vector<schemaforge::TableData> table_data =
      schemaforge::GenerationPlan::generate_table_data(analysis.sorted_tables, generation_config);

  std::cout << "Generated table data:\n";
  for (const auto& table : table_data) {
    std::cout << table << "\n\n";
  }

  std::vector<std::string> insert_statements =
      schemaforge::SqlInsertWriter::write_inserts(table_data);

  if (!write_output_file(generation_config.output_file, insert_statements)) {
    return 1;
  }

  std::cout << "Wrote SQL INSERT statements to " << generation_config.output_file << '\n';

  if (generation_config.sqlite_validation) {
    schemaforge::ValidationResult sqlite_validation_result =
        schemaforge::SQLiteValidator::validate(analysis.sql, insert_statements);

    std::cout << "\nSQLite " << sqlite_validation_result << "\n";
    if (!sqlite_validation_result.is_valid) {
      return 1;
    }
  }

  return 0;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
  if (argc < 2) {
    print_usage();
    return 1;
  }

  const std::string command = argv[1];
  if (command == "init") {
    return run_init(argc, argv);
  }

  if (command == "generate") {
    return run_generate(argc, argv);
  }

  std::cerr << "Unknown command: " << command << '\n';
  print_usage();
  return 1;
}
