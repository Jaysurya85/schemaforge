#pragma once

#include <ostream>
#include <string>

#include "schemaforge/generator/GenerationPlan.h"

namespace schemaforge {

class PostgresCopyWriter {
 private:
  std::ostream& output;
  const Table* current_table{nullptr};
  bool closed{false};

  static std::string quote_identifier(const std::string& identifier);
  static std::string escape_text(const std::string& value);
  static std::string format_field(const GeneratedValue& value);
  void ensure_output() const;

 public:
  explicit PostgresCopyWriter(std::ostream& output);
  PostgresCopyWriter(const PostgresCopyWriter&) = delete;
  PostgresCopyWriter& operator=(const PostgresCopyWriter&) = delete;

  void begin_table(const Table& table);
  void write_row(const GeneratedRow& row);
  void end_table(const Table& table);
  void close();
};

}  // namespace schemaforge
