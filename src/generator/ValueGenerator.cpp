#include "schemaforge/generator/ValueGenerator.h"

#include <algorithm>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>

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

const ForeignKey* find_foreign_key(const Table& table, const Column& column) {
  for (const auto& foreign_key : table.get_foreign_keys()) {
    if (contains_column(foreign_key.local_columns, &column)) {
      return &foreign_key;
    }
  }
  return nullptr;
}

bool is_integer_type(DataType data_type) {
  return data_type == DataType::INT || data_type == DataType::BIGINT;
}

bool is_text_type(DataType data_type) {
  return data_type == DataType::TEXT || data_type == DataType::VARCHAR;
}

bool is_decimal_type(DataType data_type) {
  return data_type == DataType::DECIMAL || data_type == DataType::FLOAT ||
         data_type == DataType::DOUBLE || data_type == DataType::REAL;
}

std::vector<Data> generate_random_int_data(int num_rows, RandomEngine& random_engine) {
  std::vector<Data> data;
  data.reserve(num_rows);

  for (int row = 0; row < num_rows; ++row) {
    data.push_back(std::to_string(random_engine.next_int(1, 1000)));
  }

  return data;
}

std::vector<Data> generate_random_decimal_data(int num_rows, RandomEngine& random_engine) {
  std::vector<Data> data;
  data.reserve(num_rows);

  for (int row = 0; row < num_rows; ++row) {
    std::ostringstream output;
    output << std::fixed << std::setprecision(2) << random_engine.next_decimal(1.0, 1000.0);
    data.push_back(output.str());
  }

  return data;
}

std::vector<Data> generate_random_boolean_data(int num_rows, RandomEngine& random_engine) {
  std::vector<Data> data;
  data.reserve(num_rows);

  for (int row = 0; row < num_rows; ++row) {
    data.emplace_back(random_engine.next_bool() ? "true" : "false");
  }

  return data;
}

}  // namespace

std::vector<Data> ValueGenerator::generate_column_data(const Column& column, const Table& table,
                                                       int num_rows,
                                                       const GenerationConfig& config,
                                                       RandomEngine& random_engine,
                                                       const KeyRegistry& key_registry) {
  const auto column_data_type = column.get_column_type().data_type;
  const bool has_unique_constraint = has_constraint(table, column, ConstraintType::Unique);
  const bool has_primary_key_constraint = has_constraint(table, column, ConstraintType::PrimaryKey);
  if (has_primary_key_constraint) {
    if (!is_integer_type(column_data_type)) {
      throw std::runtime_error("Primary key column '" + column.get_column_name() +
                               "' must use an integer type for v0.1 generation");
    }
    IntGenerator generator(1);
    return generator.generate(num_rows);
  }

  const ForeignKey* foreign_key = find_foreign_key(table, column);
  if (foreign_key != nullptr) {
    if (foreign_key->local_columns.size() != 1 || foreign_key->referenced_columns.size() != 1) {
      throw std::runtime_error("Composite foreign keys are not supported for v0.1 generation " +
                               std::to_string(foreign_key->local_columns.size()) + " local, " +
                               std::to_string(foreign_key->referenced_columns.size()) +
                               " referenced");
    }

    if (!is_integer_type(column_data_type)) {
      throw std::runtime_error("Foreign key column '" + column.get_column_name() +
                               "' must use an integer type for v0.1 generation");
    }

    if (has_unique_constraint) {
      std::vector<Data> data;
      data.reserve(num_rows);
      for (int row = 0; row < num_rows; ++row) {
        data.push_back(key_registry.key_at_row(foreign_key->referenced_table,
                                               foreign_key->referenced_columns,
                                               static_cast<std::size_t>(row))
                           .front());
      }
      return data;
    }

    std::vector<Data> data;
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
    return generate_random_int_data(num_rows, random_engine);
  }

  if (is_text_type(column_data_type)) {
    TextGenerator generator(column.get_column_name());
    return generator.generate(num_rows);
  }

  if (is_decimal_type(column_data_type)) {
    return generate_random_decimal_data(num_rows, random_engine);
  }

  if (column_data_type == DataType::BOOLEAN) {
    return generate_random_boolean_data(num_rows, random_engine);
  }

  throw std::runtime_error("Unsupported data type for column '" + column.get_column_name() + "'");
}

}  // namespace schemaforge
