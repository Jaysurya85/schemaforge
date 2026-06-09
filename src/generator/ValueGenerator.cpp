#include "schemaforge/generator/ValueGenerator.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "schemaforge/generator/BooleanGenerator.h"
#include "schemaforge/generator/DecimalGenerator.h"
#include "schemaforge/generator/TextGenerator.h"

namespace schemaforge {

namespace {

bool contains_column(const std::vector<Column*>& columns, const Column* column) {
  return std::ranges::find(columns, column) != columns.end();
}

bool has_constraint(const Table& table, const Column& column, ConstraintType constraint_type) {
  for (const auto& constraint : table.get_table_constraints()) {
    if (constraint.type == constraint_type && contains_column(constraint.columns, &column)) {
      return true;
    }
  }
  return false;
}

std::vector<Column*> key_columns_for_constraint(const Table& table, const Column& column,
                                                ConstraintType constraint_type) {
  for (const auto& constraint : table.get_table_constraints()) {
    if (constraint.type == constraint_type && contains_column(constraint.columns, &column)) {
      return constraint.columns;
    }
  }
  return {};
}

const ForeignKey* find_foreign_key(const Table& table, const Column& column) {
  for (const auto& foreign_key : table.get_foreign_keys()) {
    if (contains_column(foreign_key.local_columns, &column)) {
      return &foreign_key;
    }
  }
  return nullptr;
}

bool is_integer_type(DataType data_type) {
  return data_type == DataType::INT || data_type == DataType::BIGINT ||
         data_type == DataType::SMALLINT;
}

bool is_text_type(DataType data_type) {
  return data_type == DataType::TEXT || data_type == DataType::VARCHAR;
}

bool is_decimal_type(DataType data_type) {
  return data_type == DataType::DECIMAL || data_type == DataType::FLOAT ||
         data_type == DataType::DOUBLE || data_type == DataType::REAL;
}

int char_length(const Column& column) {
  const int64_t parsed_length = column.get_column_type().length;
  if (parsed_length <= 0) {
    return 1;
  }
  return static_cast<int>(parsed_length);
}

std::string char_value_at(int row_index, int length) {
  int value = row_index;
  std::string result(static_cast<std::size_t>(length), 'A');
  for (int index = length - 1; index >= 0; --index) {
    result[static_cast<std::size_t>(index)] = static_cast<char>('A' + (value % 26));
    value /= 26;
  }
  return result;
}

DateValue date_at(int row_index) {
  return DateValue{.year = 2026, .month = 1, .day = row_index + 1};
}

TimeValue time_at(int seconds) {
  return TimeValue{.hour = seconds / 3600, .minute = (seconds % 3600) / 60, .second = seconds % 60};
}

std::vector<GeneratedValue> generate_int_data(int num_rows) {
  std::vector<GeneratedValue> data;
  data.reserve(num_rows);

  for (int row = 0; row < num_rows; ++row) {
    data.push_back(GeneratedValue::integer(row + 1));
  }

  return data;
}

std::vector<GeneratedValue> generate_decimal_data(int num_rows) {
  std::vector<GeneratedValue> data;
  data.reserve(num_rows);

  for (int row = 0; row < num_rows; ++row) {
    data.push_back(GeneratedValue::numeric(static_cast<double>((row + 1) * 10) + 0.5));
  }

  return data;
}

std::vector<GeneratedValue> generate_boolean_data(int num_rows) {
  std::vector<GeneratedValue> data;
  data.reserve(num_rows);

  for (int row = 0; row < num_rows; ++row) {
    data.push_back(GeneratedValue::boolean(row % 2 == 0));
  }

  return data;
}

std::vector<GeneratedValue> generate_char_data(const Column& column, int num_rows) {
  std::vector<GeneratedValue> data;
  data.reserve(num_rows);
  const int length = char_length(column);
  int capacity = 1;
  for (int index = 0; index < length; ++index) {
    capacity *= 26;
  }

  for (int row = 0; row < num_rows; ++row) {
    data.push_back(GeneratedValue::text(char_value_at(row % capacity, length)));
  }

  return data;
}

std::vector<GeneratedValue> generate_date_data(int num_rows) {
  std::vector<GeneratedValue> data;
  data.reserve(num_rows);

  for (int row = 0; row < num_rows; ++row) {
    data.push_back(GeneratedValue::date(date_at(row)));
  }

  return data;
}

std::vector<GeneratedValue> generate_time_data(int num_rows) {
  std::vector<GeneratedValue> data;
  data.reserve(num_rows);

  for (int row = 0; row < num_rows; ++row) {
    data.push_back(GeneratedValue::time(time_at(row + 1)));
  }

  return data;
}

std::vector<GeneratedValue> generate_date_time_data(int num_rows) {
  std::vector<GeneratedValue> data;
  data.reserve(num_rows);

  for (int row = 0; row < num_rows; ++row) {
    data.push_back(GeneratedValue::date_time(
        DateTimeValue{.date = DateValue{.year = 2026, .month = 1, .day = 1},
                      .time = time_at(row)}));
  }

  return data;
}

std::vector<GeneratedValue> generate_key_source_data(const Table& table, const Column& column,
                                                     int num_rows,
                                                     const KeyRegistry& key_registry) {
  std::vector<GeneratedValue> data;
  data.reserve(num_rows);
  std::vector<Column*> key_columns =
      key_columns_for_constraint(table, column, ConstraintType::PrimaryKey);

  for (int row = 0; row < num_rows; ++row) {
    data.push_back(key_registry.key_at_row(&table, key_columns, static_cast<std::size_t>(row))
                       .front());
  }

  return data;
}

}  // namespace

std::vector<GeneratedValue> ValueGenerator::generate_column_data(
    const Column& column, const Table& table, int num_rows, const GenerationConfig& config,
    RandomEngine& random_engine, const KeyRegistry& key_registry) {
  const auto column_data_type = column.get_column_type().data_type;
  const bool has_unique_constraint = has_constraint(table, column, ConstraintType::Unique);
  const bool has_primary_key_constraint = has_constraint(table, column, ConstraintType::PrimaryKey);
  if (has_primary_key_constraint) {
    if (!is_integer_type(column_data_type) && !is_text_type(column_data_type) &&
        column_data_type != DataType::CHAR) {
      throw std::runtime_error("Primary key column '" + column.get_column_name() +
                               "' must use an integer, text, or CHAR type for generation");
    }
    return generate_key_source_data(table, column, num_rows, key_registry);
  }

  const ForeignKey* foreign_key = find_foreign_key(table, column);
  if (foreign_key != nullptr) {
    if (foreign_key->local_columns.size() != 1 || foreign_key->referenced_columns.size() != 1) {
      throw std::runtime_error("Composite foreign keys are not supported for v0.1 generation " +
                               std::to_string(foreign_key->local_columns.size()) + " local, " +
                               std::to_string(foreign_key->referenced_columns.size()) +
                               " referenced");
    }

    if (!is_integer_type(column_data_type) && !is_text_type(column_data_type)) {
      throw std::runtime_error("Foreign key column '" + column.get_column_name() +
                               "' must use an integer or text type for generation");
    }

    if (has_unique_constraint) {
      std::vector<GeneratedValue> data;
      data.reserve(num_rows);
      for (int row = 0; row < num_rows; ++row) {
        data.push_back(key_registry.key_at_row(foreign_key->referenced_table,
                                               foreign_key->referenced_columns,
                                               static_cast<std::size_t>(row))
                           .front());
      }
      return data;
    }

    std::vector<GeneratedValue> data;
    data.reserve(num_rows);
    for (int row = 0; row < num_rows; ++row) {
      data.push_back(key_registry
                         .random_key(foreign_key->referenced_table,
                                     foreign_key->referenced_columns, random_engine)
                         .front());
    }
    return data;
  }

  if (has_unique_constraint) {
    if (is_integer_type(column_data_type)) {
      IntGenerator generator(1);
      return generator.generate(num_rows);
    }

    if (is_text_type(column_data_type)) {
      TextGenerator generator(column.get_column_name());
      return generator.generate(num_rows);
    }

    if (column_data_type == DataType::CHAR) {
      return generate_char_data(column, num_rows);
    }

    if (is_decimal_type(column_data_type)) {
      DecimalGenerator generator(column.get_column_name());
      return generator.generate(num_rows);
    }

    if (column_data_type == DataType::BOOLEAN) {
      BooleanGenerator generator;
      return generator.generate(num_rows);
    }
  }

  if (is_integer_type(column_data_type)) {
    return generate_int_data(num_rows);
  }

  if (is_text_type(column_data_type)) {
    TextGenerator generator(column.get_column_name());
    return generator.generate(num_rows);
  }

  if (is_decimal_type(column_data_type)) {
    return generate_decimal_data(num_rows);
  }

  if (column_data_type == DataType::BOOLEAN) {
    return generate_boolean_data(num_rows);
  }

  if (column_data_type == DataType::CHAR) {
    return generate_char_data(column, num_rows);
  }

  if (column_data_type == DataType::DATE) {
    return generate_date_data(num_rows);
  }

  if (column_data_type == DataType::TIME) {
    return generate_time_data(num_rows);
  }

  if (column_data_type == DataType::DATETIME) {
    return generate_date_time_data(num_rows);
  }

  throw std::runtime_error("Unsupported data type for column '" + column.get_column_name() + "'");
}

}  // namespace schemaforge
