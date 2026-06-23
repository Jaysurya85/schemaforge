#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "schemaforge/generator/GenerationPlan.h"

namespace schemaforge {

enum class SQLiteValidationStatus {
  Failed,
  Passed,
  Skipped,
};

struct BenchmarkReport {
  std::vector<std::pair<std::string, std::size_t>> generated_rows;
  std::size_t total_rows{0};
  double generation_time_seconds{0.0};
  double validation_time_seconds{0.0};
  double total_command_time_seconds{0.0};
  std::optional<std::uint64_t> output_file_size_bytes;
  std::optional<std::uint64_t> peak_process_memory_bytes;
  SQLiteValidationStatus sqlite_validation_status{SQLiteValidationStatus::Skipped};
};

class BenchmarkEngine {
 public:
  static void record_generated_rows(BenchmarkReport& report,
                                    const std::vector<TableData>& table_data);
  static void record_configured_rows(BenchmarkReport& report,
                                     const std::vector<TablePtr>& tables,
                                     const GenerationConfig& config);
  static std::optional<std::uint64_t> output_file_size_bytes(const std::string& path);
  static std::optional<std::uint64_t> output_files_size_bytes(
      const std::vector<std::string>& paths);
  static std::optional<std::uint64_t> peak_process_memory_bytes();
  static bool write_report(const BenchmarkReport& report, const std::string& path);
};

}  // namespace schemaforge
