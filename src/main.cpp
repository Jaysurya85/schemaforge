#include <chrono>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "schemaforge/benchmark/BenchmarkEngine.h"
#include "schemaforge/config/GenerationConfig.h"
#include "schemaforge/generator/GenerationPlan.h"
#include "schemaforge/graph/DependencyGraph.h"
#include "schemaforge/io/FileReader.h"
#include "schemaforge/output/CsvWriter.h"
#include "schemaforge/output/PostgresCopyWriter.h"
#include "schemaforge/output/SqlInsertWriter.h"
#include "schemaforge/parser/ParserAdapter.h"
#include "schemaforge/schema/Table.h"
#include "schemaforge/validation/CapacityAnalyzer.h"
#include "schemaforge/validation/PostgresDockerValidator.h"
#include "schemaforge/validation/ValidationRunner.h"

namespace {

struct SchemaAnalysis {
  std::string sql;
  std::vector<schemaforge::TablePtr> sorted_tables;
  std::vector<schemaforge::TableId> table_order;
};

double elapsed_seconds(std::chrono::steady_clock::time_point start,
                       std::chrono::steady_clock::time_point end) {
  return std::chrono::duration<double>(end - start).count();
}

std::string format_mebibytes(const std::optional<std::uint64_t>& bytes) {
  if (!bytes.has_value()) {
    return "unavailable";
  }

  constexpr double bytes_per_mebibyte = 1024.0 * 1024.0;
  std::ostringstream formatted;
  formatted << std::fixed << std::setprecision(1)
            << static_cast<double>(*bytes) / bytes_per_mebibyte << " MiB";
  return formatted.str();
}

void print_usage() {
  std::cerr << "Usage:\n"
            << "  schemaforge init [--schema schema.sql] [--config schemaforge.yaml] "
               "[--seed 42] [--default-rows 10]\n"
            << "  schemaforge generate [--config schemaforge.yaml]\n";
}

SchemaAnalysis analyze_schema(const std::string& schema_path, bool verbose) {
  schemaforge::ValidationResult schema_file_validation =
      schemaforge::ValidationRunner::validate_schema_file(schema_path);
  if (!schema_file_validation.is_valid) {
    std::cout << schema_file_validation << "\n";
    return {.sql = {}, .sorted_tables = {}, .table_order = {}};
  }

  std::string sql = schemaforge::FileReader::read_file(schema_path);

  std::cout << "Welcome to Schemaforge" << '\n';
  std::cout << "Schema file: " << schema_path << '\n';
  if (verbose) {
    std::cout << "Parsing SQL:\n" << sql << "\n\n";
  }

  std::vector<schemaforge::TablePtr> tables = schemaforge::ParserAdapter::parse(sql);
  schemaforge::ValidationResult validation_result =
      schemaforge::ValidationRunner::validate_schema(tables);

  std::cout << validation_result << "\n";
  if (!validation_result.is_valid) {
    return {.sql = std::move(sql), .sorted_tables = {}, .table_order = {}};
  }

  schemaforge::ParserAdapter::foreign_key_resolver(tables);

  std::cout << tables.size() << " tables parsed.\n";
  if (verbose) {
    for (const auto& table : tables) {
      std::cout << *table << "\n\n";
    }
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

  if (verbose) {
    std::cout << dependency_graph << "\n";
  }

  std::cout << "Topologically sorted tables:\n";
  for (const auto& table_ptr : sorted_tables) {
    std::cout << table_ptr->get_table_name() << "\n";
  }

  return {.sql = std::move(sql),
          .sorted_tables = std::move(sorted_tables),
          .table_order = std::move(table_order)};
}

int run_init(int argc, char* argv[]) {
  std::string config_path = "schemaforge.yaml";
  schemaforge::GenerationConfig generation_config = schemaforge::GenerationConfig::make_default();

  if (!generation_config.apply_init_args(argc, argv, 2, config_path)) {
    return 1;
  }

  SchemaAnalysis analysis = analyze_schema(generation_config.schema_path, true);
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
  const auto command_start = std::chrono::steady_clock::now();
  schemaforge::BenchmarkReport benchmark_report;

  if (generation_config.output_format != "sql" && generation_config.output_format != "csv" &&
      generation_config.output_format != "postgres_copy") {
    std::cerr << "Unsupported output format: " << generation_config.output_format << '\n';
    return 1;
  }

  SchemaAnalysis analysis = analyze_schema(generation_config.schema_path, false);
  if (analysis.table_order.empty()) {
    return 1;
  }

  schemaforge::ValidationResult config_validation_result =
      schemaforge::ValidationRunner::validate_config(analysis.sorted_tables, generation_config);
  std::cout << "Config " << config_validation_result << "\n";
  if (!config_validation_result.is_valid) {
    return 1;
  }

  schemaforge::SchemaCapacityInfo capacity_info =
      schemaforge::CapacityAnalyzer::analyze(analysis.sorted_tables, generation_config);

  schemaforge::ValidationResult generation_validation_result =
      schemaforge::ValidationRunner::validate_generation(analysis.sorted_tables, generation_config,
                                                         capacity_info);

  std::cout << "Generation " << generation_validation_result << "\n";
  if (!generation_validation_result.is_valid) {
    return 1;
  }

  const auto generation_start = std::chrono::steady_clock::now();
  std::vector<std::string> output_files;
  try {
    if (generation_config.output_format == "sql") {
      std::ofstream output_file(generation_config.output_file);
      if (!output_file.is_open()) {
        throw std::runtime_error("Failed to open output file: " + generation_config.output_file);
      }

      schemaforge::GenerationPlan::stream_table_data(
          analysis.sorted_tables, generation_config,
          [&output_file](const schemaforge::GeneratedRow& row) {
            schemaforge::SqlInsertWriter::write_row(output_file, row);
          });
      output_file.close();
      if (!output_file) {
        throw std::runtime_error("Failed to write output file: " + generation_config.output_file);
      }
      output_files.push_back(generation_config.output_file);
    } else if (generation_config.output_format == "csv") {
      schemaforge::CsvWriter csv_writer(generation_config.output_directory,
                                        analysis.sorted_tables);
      schemaforge::GenerationPlan::stream_table_data(
          analysis.sorted_tables, generation_config,
          [&csv_writer](const schemaforge::GeneratedRow& row) { csv_writer.write_row(row); });
      csv_writer.close();
      output_files = csv_writer.get_output_files();
    } else {
      std::ofstream output_file(generation_config.output_file, std::ios::binary);
      if (!output_file.is_open()) {
        throw std::runtime_error("Failed to open output file: " + generation_config.output_file);
      }

      schemaforge::PostgresCopyWriter copy_writer(output_file);
      schemaforge::GenerationPlan::stream_table_data(
          analysis.sorted_tables, generation_config,
          schemaforge::GenerationStreamConsumer{
              .table_started = [&copy_writer](const schemaforge::Table& table) {
                copy_writer.begin_table(table);
              },
              .row_generated = [&copy_writer](const schemaforge::GeneratedRow& row) {
                copy_writer.write_row(row);
              },
              .table_finished = [&copy_writer](const schemaforge::Table& table) {
                copy_writer.end_table(table);
              }});
      copy_writer.close();
      output_file.close();
      if (!output_file) {
        throw std::runtime_error("Failed to write output file: " + generation_config.output_file);
      }
      output_files.push_back(generation_config.output_file);
    }
  } catch (const std::exception& error) {
    std::string output_description = "PostgreSQL COPY output";
    if (generation_config.output_format == "sql") {
      output_description = "SQL INSERT statements";
    } else if (generation_config.output_format == "csv") {
      output_description = "CSV files";
    }
    std::cerr << "Failed to generate " << output_description << ": " << error.what() << '\n';
    return 1;
  }

  const auto generation_end = std::chrono::steady_clock::now();
  benchmark_report.generation_time_seconds = elapsed_seconds(generation_start, generation_end);
  schemaforge::BenchmarkEngine::record_configured_rows(benchmark_report, analysis.sorted_tables,
                                                       generation_config);
  benchmark_report.output_file_size_bytes =
      schemaforge::BenchmarkEngine::output_files_size_bytes(output_files);

  if (generation_config.output_format == "sql") {
    std::cout << "Wrote SQL INSERT statements to " << generation_config.output_file << '\n';
  } else if (generation_config.output_format == "csv") {
    std::cout << "Wrote CSV files to " << generation_config.output_directory << '\n';
  } else {
    std::cout << "Wrote PostgreSQL COPY data to " << generation_config.output_file << '\n';
  }

  if (generation_config.output_format == "sql" && generation_config.sqlite_validation) {
    const auto validation_start = std::chrono::steady_clock::now();
    schemaforge::ValidationResult sqlite_validation_result =
        schemaforge::ValidationRunner::validate_sqlite_file(analysis.sql,
                                                            generation_config.output_file);
    const auto validation_end = std::chrono::steady_clock::now();
    benchmark_report.validation_time_seconds = elapsed_seconds(validation_start, validation_end);
    benchmark_report.sqlite_validation_status = sqlite_validation_result.is_valid
                                                    ? schemaforge::SQLiteValidationStatus::Passed
                                                    : schemaforge::SQLiteValidationStatus::Failed;

    std::cout << "\nSQLite " << sqlite_validation_result << "\n";
    if (!sqlite_validation_result.is_valid) {
      benchmark_report.total_command_time_seconds =
          elapsed_seconds(command_start, std::chrono::steady_clock::now());
      benchmark_report.peak_process_memory_bytes =
          schemaforge::BenchmarkEngine::peak_process_memory_bytes();
      schemaforge::BenchmarkEngine::write_report(benchmark_report,
                                                 generation_config.benchmark_file);
      return 1;
    }
  } else {
    benchmark_report.sqlite_validation_status = schemaforge::SQLiteValidationStatus::Skipped;
    if (generation_config.output_format == "csv" && generation_config.sqlite_validation) {
      std::cout << "\nSQLite validation skipped for CSV output.\n";
    } else if (generation_config.output_format == "postgres_copy" &&
               generation_config.sqlite_validation) {
      std::cout << "\nSQLite validation skipped for PostgreSQL COPY output.\n";
    }
  }

  if (generation_config.postgres_validation) {
    const auto postgres_validation_start = std::chrono::steady_clock::now();
    const schemaforge::PostgresDockerValidationResult postgres_result =
        schemaforge::PostgresDockerValidator::validate(
            generation_config.schema_path, analysis.sorted_tables, generation_config);
    const auto postgres_validation_end = std::chrono::steady_clock::now();
    benchmark_report.postgres_validation_time_seconds =
        elapsed_seconds(postgres_validation_start, postgres_validation_end);

    if (postgres_result.status == schemaforge::PostgresDockerValidationStatus::Passed) {
      benchmark_report.postgres_validation_status =
          schemaforge::PostgresValidationStatus::Passed;
      std::cout << "PostgreSQL Docker Validation Result: Valid\n";
    } else if (postgres_result.status ==
               schemaforge::PostgresDockerValidationStatus::Unavailable) {
      benchmark_report.postgres_validation_status =
          schemaforge::PostgresValidationStatus::Unavailable;
      std::cout << "PostgreSQL Docker validation unavailable";
      if (!postgres_result.errors.empty()) {
        std::cout << ": " << postgres_result.errors.front();
      }
      std::cout << '\n';
    } else {
      benchmark_report.postgres_validation_status =
          schemaforge::PostgresValidationStatus::Failed;
      std::cerr << "PostgreSQL Docker Validation Result: Invalid\n";
      for (const auto& error : postgres_result.errors) {
        std::cerr << "- " << error << '\n';
      }
      benchmark_report.total_command_time_seconds =
          elapsed_seconds(command_start, std::chrono::steady_clock::now());
      benchmark_report.peak_process_memory_bytes =
          schemaforge::BenchmarkEngine::peak_process_memory_bytes();
      schemaforge::BenchmarkEngine::write_report(benchmark_report,
                                                 generation_config.benchmark_file);
      return 1;
    }
  }

  benchmark_report.total_command_time_seconds =
      elapsed_seconds(command_start, std::chrono::steady_clock::now());
  benchmark_report.peak_process_memory_bytes =
      schemaforge::BenchmarkEngine::peak_process_memory_bytes();
  if (!schemaforge::BenchmarkEngine::write_report(benchmark_report,
                                                  generation_config.benchmark_file)) {
    return 1;
  }

  std::cout << "Wrote benchmark report to " << generation_config.benchmark_file << '\n';
  std::cout << "Total rows generated: " << benchmark_report.total_rows << '\n';
  std::cout << "Generation time: " << benchmark_report.generation_time_seconds << "s\n";
  std::cout << "Output file size: "
            << format_mebibytes(benchmark_report.output_file_size_bytes) << '\n';
  std::cout << "Peak process memory usage: "
            << format_mebibytes(benchmark_report.peak_process_memory_bytes) << '\n';

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
