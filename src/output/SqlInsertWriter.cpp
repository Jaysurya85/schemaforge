#include "schemaforge/output/SqlInsertWriter.h"

#include <sstream>

namespace schemaforge {

bool SqlInsertWriter::should_quote(DataType data_type) {
  switch (data_type) {
    case DataType::CHAR:
    case DataType::DATE:
    case DataType::DATETIME:
    case DataType::TEXT:
    case DataType::TIME:
    case DataType::VARCHAR:
      return true;
    case DataType::BIGINT:
    case DataType::BOOLEAN:
    case DataType::DECIMAL:
    case DataType::DOUBLE:
    case DataType::FLOAT:
    case DataType::INT:
    case DataType::LONG:
    case DataType::REAL:
    case DataType::SMALLINT:
    case DataType::UNKNOWN:
    default:
      return false;
  }
}

std::string SqlInsertWriter::escape_sql_string(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());

  for (const char character : value) {
    if (character == '\'') {
      escaped += "''";
      continue;
    }
    escaped += character;
  }

  return escaped;
}

std::string SqlInsertWriter::format_value(const Column& column, const Data& value) {
  if (!should_quote(column.get_column_type().data_type)) {
    return value;
  }

  return "'" + escape_sql_string(value) + "'";
}

std::string SqlInsertWriter::write_row(const TableData& table_data, std::size_t row_index) {
  std::ostringstream output;

  output << "INSERT INTO " << table_data.table_name << " (";
  for (std::size_t column_index = 0; column_index < table_data.columns.size(); ++column_index) {
    if (column_index > 0) {
      output << ", ";
    }
    output << table_data.columns[column_index].column.get_column_name();
  }

  output << ") VALUES (";
  for (std::size_t column_index = 0; column_index < table_data.columns.size(); ++column_index) {
    if (column_index > 0) {
      output << ", ";
    }

    const auto& column_data = table_data.columns[column_index];
    output << format_value(column_data.column, column_data.data[row_index]);
  }
  output << ");";

  return output.str();
}

std::vector<std::string> SqlInsertWriter::write_inserts(const std::vector<TableData>& tables) {
  std::vector<std::string> inserts;

  for (const auto& table_data : tables) {
    if (table_data.columns.empty()) {
      continue;
    }

    const std::size_t row_count = table_data.columns.front().data.size();
    inserts.reserve(inserts.size() + row_count);

    for (std::size_t row_index = 0; row_index < row_count; ++row_index) {
      inserts.push_back(write_row(table_data, row_index));
    }
  }

  return inserts;
}

}  // namespace schemaforge
