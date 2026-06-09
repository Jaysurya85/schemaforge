#include "schemaforge/output/SqlInsertWriter.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace schemaforge {

namespace {

template <class... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

std::string format_numeric(double value) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << value;
  return output.str();
}

std::string zero_padded(int value, int width) {
  std::ostringstream output;
  output << std::setw(width) << std::setfill('0') << value;
  return output.str();
}

std::string format_date(const DateValue& value) {
  return zero_padded(value.year, 4) + "-" + zero_padded(value.month, 2) + "-" +
         zero_padded(value.day, 2);
}

std::string format_time(const TimeValue& value) {
  return zero_padded(value.hour, 2) + ":" + zero_padded(value.minute, 2) + ":" +
         zero_padded(value.second, 2);
}

}  // namespace

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

std::string SqlInsertWriter::format_value(const Column& column, const GeneratedValue& value) {
  (void)column;
  return value.visit(Overloaded{
      [](std::monostate) { return std::string("NULL"); },
      [](std::int64_t integer_value) { return std::to_string(integer_value); },
      [](double numeric_value) { return format_numeric(numeric_value); },
      [](const std::string& text_value) { return "'" + escape_sql_string(text_value) + "'"; },
      [](bool boolean_value) { return std::string(boolean_value ? "true" : "false"); },
      [](const DateValue& date_value) { return "'" + format_date(date_value) + "'"; },
      [](const TimeValue& time_value) { return "'" + format_time(time_value) + "'"; },
      [](const DateTimeValue& date_time_value) {
        return "'" + format_date(date_time_value.date) + " " + format_time(date_time_value.time) +
               "'";
      },
  });
}

std::string SqlInsertWriter::write_row(const TableData& table_data, std::size_t row_index) {
  std::ostringstream output;

  output << "INSERT INTO " << table_data.table->get_table_name() << " (";
  for (std::size_t column_index = 0; column_index < table_data.columns.size(); ++column_index) {
    if (column_index > 0) {
      output << ", ";
    }
    output << table_data.columns[column_index].column->get_column_name();
  }

  output << ") VALUES (";
  for (std::size_t column_index = 0; column_index < table_data.columns.size(); ++column_index) {
    if (column_index > 0) {
      output << ", ";
    }

    const auto& column_data = table_data.columns[column_index];
    output << format_value(*column_data.column, column_data.data[row_index]);
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
