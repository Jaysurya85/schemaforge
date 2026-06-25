#include "schemaforge/output/CsvWriter.h"

#include <cctype>
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

std::string CsvWriter::escape_text(const std::string& value) {
  const bool has_surrounding_whitespace =
      !value.empty() &&
      (std::isspace(static_cast<unsigned char>(value.front())) != 0 ||
       std::isspace(static_cast<unsigned char>(value.back())) != 0);
  const bool needs_quotes = value.empty() || has_surrounding_whitespace ||
                            value.find_first_of(",\"\r\n") != std::string::npos;
  if (!needs_quotes) {
    return value;
  }

  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back('"');
  for (const char character : value) {
    if (character == '"') {
      escaped += "\"\"";
    } else {
      escaped.push_back(character);
    }
  }
  escaped.push_back('"');
  return escaped;
}

std::string CsvWriter::format_field(const GeneratedValue& value) {
  return value.visit(Overloaded{
      [](std::monostate) { return std::string(); },
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

void CsvWriter::validate_table_name(const std::string& table_name) {
  const std::filesystem::path name_path(table_name);
  if (table_name.empty() || table_name == "." || table_name == ".." || name_path.is_absolute() ||
      name_path.filename() != name_path || table_name.find('\\') != std::string::npos) {
    throw std::runtime_error("Table name '" + table_name +
                             "' cannot be used as a CSV file name");
  }
}

CsvWriter::CsvWriter(const std::string& directory, const std::vector<TablePtr>& tables)
    : output_directory(directory) {
  std::error_code error;
  std::filesystem::create_directories(output_directory, error);
  if (error || !std::filesystem::is_directory(output_directory)) {
    throw std::runtime_error("Failed to create CSV output directory: " +
                             output_directory.string());
  }

  table_paths.reserve(tables.size());
  output_files.reserve(tables.size());
  for (const auto& table_ptr : tables) {
    const Table* table = table_ptr.get();
    const std::string table_name = table->get_table_name();
    validate_table_name(table_name);

    const std::filesystem::path path = output_directory / (table_name + ".csv");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
      throw std::runtime_error("Failed to open CSV output file: " + path.string());
    }
    write_header(output, *table);
    output.close();
    if (!output) {
      throw std::runtime_error("Failed to write CSV header: " + path.string());
    }

    table_paths.emplace(table, path);
    output_files.push_back(path.string());
  }
}

void CsvWriter::write_header(std::ostream& output, const Table& table) {
  const auto& columns = table.get_columns();
  for (std::size_t index = 0; index < columns.size(); ++index) {
    if (index > 0) {
      output << ',';
    }
    output << escape_text(columns[index]->get_column_name());
  }
  output << '\n';
}

void CsvWriter::write_row(std::ostream& output, const GeneratedRow& row) {
  if (row.columns.size() != row.values.size()) {
    throw std::runtime_error("CSV row column and value counts do not match");
  }

  for (std::size_t index = 0; index < row.values.size(); ++index) {
    if (index > 0) {
      output << ',';
    }
    output << format_field(row.values[index]);
  }
  output << '\n';
}

void CsvWriter::open_table(const Table* table) {
  const auto table_path = table_paths.find(table);
  if (table_path == table_paths.end()) {
    throw std::runtime_error("CSV row references an unregistered table");
  }

  if (current_output.is_open()) {
    current_output.close();
    if (!current_output) {
      throw std::runtime_error("Failed to finish CSV output for table '" +
                               current_table->get_table_name() + "'");
    }
  }

  current_output.clear();
  current_output.open(table_path->second, std::ios::binary | std::ios::app);
  if (!current_output.is_open()) {
    throw std::runtime_error("Failed to open CSV output file: " + table_path->second.string());
  }
  current_table = table;
}

void CsvWriter::write_row(const GeneratedRow& row) {
  if (row.table != current_table) {
    open_table(row.table);
  }
  write_row(current_output, row);
  if (!current_output) {
    throw std::runtime_error("Failed to write CSV row for table '" +
                             row.table->get_table_name() + "'");
  }
}

void CsvWriter::close() {
  if (!current_output.is_open()) {
    return;
  }
  current_output.close();
  if (!current_output) {
    throw std::runtime_error("Failed to finish CSV output for table '" +
                             current_table->get_table_name() + "'");
  }
}

const std::vector<std::string>& CsvWriter::get_output_files() const {
  return output_files;
}

}  // namespace schemaforge
