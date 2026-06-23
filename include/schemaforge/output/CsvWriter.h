#pragma once

#include <filesystem>
#include <fstream>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "schemaforge/generator/GenerationPlan.h"

namespace schemaforge {

class CsvWriter {
 private:
  std::filesystem::path output_directory;
  std::unordered_map<const Table*, std::filesystem::path> table_paths;
  std::vector<std::string> output_files;
  std::ofstream current_output;
  const Table* current_table{nullptr};

  static std::string format_field(const GeneratedValue& value);
  static std::string escape_text(const std::string& value);
  static void validate_table_name(const std::string& table_name);
  void open_table(const Table* table);

 public:
  CsvWriter(const std::string& directory, const std::vector<TablePtr>& tables);
  CsvWriter(const CsvWriter&) = delete;
  CsvWriter& operator=(const CsvWriter&) = delete;

  static void write_header(std::ostream& output, const Table& table);
  static void write_row(std::ostream& output, const GeneratedRow& row);
  void write_row(const GeneratedRow& row);
  void close();

  [[nodiscard]] const std::vector<std::string>& get_output_files() const;
};

}  // namespace schemaforge
