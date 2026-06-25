#pragma once
#include <cstddef>
#include <ostream>
#include <string>
#include <vector>

#include "schemaforge/config/GenerationConfig.h"
#include "schemaforge/generator/GenerationPlan.h"
#include "schemaforge/schema/Column.h"

namespace schemaforge {

class SqlInsertWriter {
 private:
  static std::string quote_identifier(const std::string& identifier, SqlDialect dialect);
  static std::string escape_sql_string(const std::string& value);
  static std::string format_value(const Column& column, const GeneratedValue& value);
  static std::string write_row(const TableData& table_data, std::size_t row_index,
                               SqlDialect dialect);
  static std::string write_row(const GeneratedRow& row, SqlDialect dialect);

 public:
  SqlInsertWriter() = default;
  static void write_row(std::ostream& output, const GeneratedRow& row);
  static void write_row(std::ostream& output, const GeneratedRow& row, SqlDialect dialect);
  static std::vector<std::string> write_inserts(const std::vector<TableData>& tables);
  static std::vector<std::string> write_inserts(const std::vector<TableData>& tables,
                                                SqlDialect dialect);
};

}  // namespace schemaforge
