#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "schemaforge/generator/GenerationPlan.h"
#include "schemaforge/schema/Column.h"

namespace schemaforge {

class SqlInsertWriter {
 private:
  static bool should_quote(DataType data_type);
  static std::string escape_sql_string(const std::string& value);
  static std::string format_value(const Column& column, const Data& value);
  static std::string write_row(const TableData& table_data, std::size_t row_index);

 public:
  SqlInsertWriter() = default;
  static std::vector<std::string> write_inserts(const std::vector<TableData>& tables);
};

}  // namespace schemaforge
