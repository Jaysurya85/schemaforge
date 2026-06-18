#include "schemaforge/benchmark/BenchmarkEngine.h"

#include <fstream>
#include <iostream>

#include "yaml-cpp/yaml.h"

namespace schemaforge {

namespace {

std::string sqlite_status_to_string(SQLiteValidationStatus status) {
  switch (status) {
    case SQLiteValidationStatus::Failed:
      return "failed";
    case SQLiteValidationStatus::Passed:
      return "passed";
    case SQLiteValidationStatus::Skipped:
      return "skipped";
  }

  return "skipped";
}

double generation_throughput(const BenchmarkReport& report) {
  if (report.generation_time_seconds <= 0.0) {
    return 0.0;
  }

  return static_cast<double>(report.total_rows) / report.generation_time_seconds;
}

}  // namespace

void BenchmarkEngine::record_generated_rows(BenchmarkReport& report,
                                            const std::vector<TableData>& table_data) {
  report.generated_rows.clear();
  report.total_rows = 0;

  for (const auto& table : table_data) {
    std::size_t row_count = 0;
    if (!table.columns.empty()) {
      row_count = table.columns.front().data.size();
    }

    const std::string table_name = table.table->get_table_name();
    report.generated_rows.emplace_back(table_name, row_count);
    report.total_rows += row_count;
  }
}

void BenchmarkEngine::record_configured_rows(BenchmarkReport& report,
                                             const std::vector<TablePtr>& tables,
                                             const GenerationConfig& config) {
  report.generated_rows.clear();
  report.total_rows = 0;

  for (const auto& table : tables) {
    const std::string table_name = table->get_table_name();
    const int row_count = config.get_row_count(table_name);
    const std::size_t normalized_row_count =
        row_count < 0 ? 0 : static_cast<std::size_t>(row_count);
    report.generated_rows.emplace_back(table_name, normalized_row_count);
    report.total_rows += normalized_row_count;
  }
}

bool BenchmarkEngine::write_report(const BenchmarkReport& report, const std::string& path) {
  YAML::Emitter yaml;
  yaml << YAML::BeginMap;
  yaml << YAML::Key << "generation";
  yaml << YAML::Value << YAML::BeginMap;
  yaml << YAML::Key << "rows";
  yaml << YAML::Value << YAML::BeginMap;
  for (const auto& [table_name, row_count] : report.generated_rows) {
    yaml << YAML::Key << table_name << YAML::Value << row_count;
  }
  yaml << YAML::EndMap;
  yaml << YAML::Key << "total_rows" << YAML::Value << report.total_rows;
  yaml << YAML::Key << "time_seconds" << YAML::Value << report.generation_time_seconds;
  yaml << YAML::Key << "throughput_rows_per_second" << YAML::Value
       << generation_throughput(report);
  yaml << YAML::EndMap;

  yaml << YAML::Key << "validation";
  yaml << YAML::Value << YAML::BeginMap;
  yaml << YAML::Key << "sqlite" << YAML::Value
       << sqlite_status_to_string(report.sqlite_validation_status);
  yaml << YAML::Key << "time_seconds" << YAML::Value << report.validation_time_seconds;
  yaml << YAML::EndMap;

  yaml << YAML::Key << "total";
  yaml << YAML::Value << YAML::BeginMap;
  yaml << YAML::Key << "command_time_seconds" << YAML::Value
       << report.total_command_time_seconds;
  yaml << YAML::EndMap;
  yaml << YAML::EndMap;

  if (!yaml.good()) {
    std::cerr << "Failed to create benchmark YAML: " << yaml.GetLastError() << '\n';
    return false;
  }

  std::ofstream benchmark_file(path);
  if (!benchmark_file.is_open()) {
    std::cerr << "Failed to open benchmark file: " << path << '\n';
    return false;
  }

  benchmark_file << yaml.c_str() << '\n';
  return true;
}

}  // namespace schemaforge
