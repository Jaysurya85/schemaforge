#include "schemaforge/validation/GenerationFeasibilityValidator.h"

#include <algorithm>

namespace schemaforge {

bool GenerationFeasibilityValidator::contains_column(const std::vector<std::string>& column_names,
                                                     const std::string& column_name) {
  return std::ranges::find(column_names, column_name) != column_names.end();
}

bool GenerationFeasibilityValidator::has_constraint(const Table& table, const Column& column,
                                                    ConstraintType constraint_type) {
  const auto column_name = column.get_column_name();
  for (const auto& constraint : table.get_table_constraints()) {
    if (constraint.type == constraint_type &&
        contains_column(constraint.columnNames, column_name)) {
      return true;
    }
  }
  return false;
}

bool GenerationFeasibilityValidator::is_supported_generation_type(DataType data_type) {
  return data_type == DataType::INT || data_type == DataType::BIGINT ||
         data_type == DataType::TEXT || data_type == DataType::VARCHAR ||
         data_type == DataType::DECIMAL || data_type == DataType::FLOAT ||
         data_type == DataType::DOUBLE || data_type == DataType::REAL ||
         data_type == DataType::BOOLEAN;
}

bool GenerationFeasibilityValidator::is_integer_type(DataType data_type) {
  return data_type == DataType::INT || data_type == DataType::BIGINT;
}

ValidationResult GenerationFeasibilityValidator::validate(const std::vector<Table>& tables,
                                                          const GenerationConfig& config) {
  ValidationResult validation_result(true, {});

  for (const auto& table : tables) {
    const int table_rows = config.get_row_count(table.get_table_name());
    if (table_rows < 0) {
      validation_result.is_valid = false;
      validation_result.errors.push_back("Row count cannot be negative for table '" +
                                         table.get_table_name() + "'");
    }

    for (const auto& column : table.get_columns()) {
      const auto data_type = column.get_column_type().data_type;
      if (!is_supported_generation_type(data_type)) {
        validation_result.is_valid = false;
        validation_result.errors.push_back("Column '" + table.get_table_name() + "." +
                                           column.get_column_name() +
                                           "' uses an unsupported generation type");
      }

      if (has_constraint(table, column, ConstraintType::PrimaryKey) &&
          !is_integer_type(data_type)) {
        validation_result.is_valid = false;
        validation_result.errors.push_back("Primary key column '" + table.get_table_name() + "." +
                                           column.get_column_name() +
                                           "' must use INT or BIGINT for v0.1 generation");
      }

      if (has_constraint(table, column, ConstraintType::Unique) && data_type == DataType::BOOLEAN &&
          table_rows > 2) {
        validation_result.is_valid = false;
        validation_result.errors.push_back("Cannot generate " + std::to_string(table_rows) +
                                           " rows for UNIQUE BOOLEAN column '" +
                                           table.get_table_name() + "." + column.get_column_name() +
                                           "' because BOOLEAN has only 2 possible values");
      }
    }

    for (const auto& foreign_key : table.get_foreign_keys()) {
      const int referenced_rows = config.get_row_count(foreign_key.referenced_table);
      if (table_rows > 0 && referenced_rows <= 0) {
        validation_result.is_valid = false;
        validation_result.errors.push_back(
            "Cannot generate " + table.get_table_name() + ": it references table '" +
            foreign_key.referenced_table + "', but that table has 0 rows");
      }

      for (const auto& local_column_name : foreign_key.local_columns) {
        const auto columns = table.get_columns();
        const auto column =
            std::ranges::find_if(columns, [&local_column_name](const Column& candidate) {
              return candidate.get_column_name() == local_column_name;
            });

        if (column != columns.end() && !is_integer_type(column->get_column_type().data_type)) {
          validation_result.is_valid = false;
          validation_result.errors.push_back("Foreign key column '" + table.get_table_name() + "." +
                                             local_column_name +
                                             "' must use INT or BIGINT for v0.1 generation");
        }
      }
    }
  }

  return validation_result;
}

}  // namespace schemaforge
