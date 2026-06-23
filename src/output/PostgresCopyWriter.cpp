#include "schemaforge/output/PostgresCopyWriter.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <type_traits>

namespace schemaforge {

namespace {

template <class... Ts>
struct Overloaded : Ts... {
  using Ts::operator()...;
};

template <class... Ts>
Overloaded(Ts...) -> Overloaded<Ts...>;

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

std::string format_numeric(double value) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(2) << value;
  return output.str();
}

}  // namespace

std::string PostgresCopyWriter::quote_identifier(const std::string& identifier) {
  std::string quoted;
  quoted.reserve(identifier.size() + 2);
  quoted.push_back('"');
  for (const char character : identifier) {
    if (character == '"') {
      quoted += "\"\"";
    } else {
      quoted.push_back(character);
    }
  }
  quoted.push_back('"');
  return quoted;
}

std::string PostgresCopyWriter::escape_text(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char character : value) {
    switch (character) {
      case '\\':
        escaped += "\\\\";
        break;
      case '\b':
        escaped += "\\b";
        break;
      case '\f':
        escaped += "\\f";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      case '\v':
        escaped += "\\v";
        break;
      default:
        escaped.push_back(character);
        break;
    }
  }
  return escaped;
}

std::string PostgresCopyWriter::format_field(const GeneratedValue& value) {
  return value.visit(Overloaded{
      [](std::monostate) { return std::string("\\N"); },
      [](std::int64_t integer_value) { return std::to_string(integer_value); },
      [](double numeric_value) { return format_numeric(numeric_value); },
      [](const std::string& text_value) { return escape_text(text_value); },
      [](bool boolean_value) { return std::string(boolean_value ? "true" : "false"); },
      [](const DateValue& date_value) { return format_date(date_value); },
      [](const TimeValue& time_value) { return format_time(time_value); },
      [](const DateTimeValue& date_time_value) {
        return format_date(date_time_value.date) + " " + format_time(date_time_value.time);
      },
  });
}

void PostgresCopyWriter::ensure_output() const {
  if (!output) {
    throw std::runtime_error("Failed to write PostgreSQL COPY output");
  }
}

PostgresCopyWriter::PostgresCopyWriter(std::ostream& output) : output(output) {
  output << "BEGIN;\n";
  ensure_output();
}

void PostgresCopyWriter::begin_table(const Table& table) {
  if (closed) {
    throw std::runtime_error("Cannot start a PostgreSQL COPY table after the writer is closed");
  }
  if (current_table != nullptr) {
    throw std::runtime_error("Cannot start PostgreSQL COPY table '" + table.get_table_name() +
                             "' before finishing table '" + current_table->get_table_name() +
                             "'");
  }

  output << "COPY " << quote_identifier(table.get_table_name()) << " (";
  const auto& columns = table.get_columns();
  for (std::size_t index = 0; index < columns.size(); ++index) {
    if (index > 0) {
      output << ", ";
    }
    output << quote_identifier(columns[index]->get_column_name());
  }
  output << ") FROM STDIN WITH (FORMAT text);\n";
  ensure_output();
  current_table = &table;
}

void PostgresCopyWriter::write_row(const GeneratedRow& row) {
  if (closed || current_table == nullptr) {
    throw std::runtime_error("Cannot write a PostgreSQL COPY row without an active table");
  }
  if (row.table != current_table) {
    throw std::runtime_error("PostgreSQL COPY row does not match the active table");
  }
  if (row.columns.size() != row.values.size()) {
    throw std::runtime_error("PostgreSQL COPY row column and value counts do not match");
  }

  for (std::size_t index = 0; index < row.values.size(); ++index) {
    if (index > 0) {
      output << '\t';
    }
    output << format_field(row.values[index]);
  }
  output << '\n';
  ensure_output();
}

void PostgresCopyWriter::end_table(const Table& table) {
  if (closed || current_table == nullptr || current_table != &table) {
    throw std::runtime_error("PostgreSQL COPY table lifecycle is out of order");
  }
  output << "\\.\n";
  ensure_output();
  current_table = nullptr;
}

void PostgresCopyWriter::close() {
  if (closed) {
    return;
  }
  if (current_table != nullptr) {
    throw std::runtime_error("Cannot finish PostgreSQL COPY output with an active table");
  }
  output << "COMMIT;\n";
  ensure_output();
  closed = true;
}

}  // namespace schemaforge
