#include "schemaforge/validation/GenerationFeasibilityValidator.h"

#include <algorithm>

namespace schemaforge {

namespace {

bool contains_column_name(const std::vector<std::string>& column_names,
                          const std::string& column_name) {
  return std::ranges::find(column_names, column_name) != column_names.end();
}

bool column_has_constraint(const Table& table, const Column& column,
                           ConstraintType constraint_type) {
  const auto column_name = column.get_column_name();
  for (const auto& constraint : table.get_table_constraints()) {
    if (constraint.type == constraint_type &&
        contains_column_name(constraint.columnNames, column_name)) {
      return true;
    }
  }
  return false;
}

const Table* find_table(const std::vector<Table>& tables, const std::string& table_name) {
  const auto table = std::ranges::find_if(tables, [&table_name](const Table& candidate) {
    return candidate.get_table_name() == table_name;
  });

  if (table == tables.end()) {
    return nullptr;
  }

  return &*table;
}

const Column* find_column(const Table& table, const std::string& column_name,
                          std::vector<Column>& columns) {
  columns = table.get_columns();
  const auto column = std::ranges::find_if(columns, [&column_name](const Column& candidate) {
    return candidate.get_column_name() == column_name;
  });

  if (column == columns.end()) {
    return nullptr;
  }

  return &*column;
}

RowCapacityLimit unique_foreign_key_capacity(
    const Table& table, const Column& column, const ForeignKey& foreign_key, int referenced_rows) {
  return RowCapacityLimit{
      .max_rows = referenced_rows,
      .reason = "UNIQUE foreign key '" + table.get_table_name() + "." +
                column.get_column_name() + "' because referenced table '" +
                foreign_key.referenced_table + "' has only " + std::to_string(referenced_rows) +
                " rows"};
}

void validate_capacity_limit(ValidationResult& validation_result, const std::string& table_name,
                             int requested_rows, const RowCapacityLimit& limit) {
  if (requested_rows <= limit.max_rows) {
    return;
  }

  validation_result.is_valid = false;
  validation_result.errors.push_back("Cannot generate " + std::to_string(requested_rows) +
                                     " rows for table '" + table_name + "': " + limit.reason +
                                     " limits the table to " +
                                     std::to_string(limit.max_rows) + " rows");
}

void validate_static_capacity(ValidationResult& validation_result, const std::string& table_name,
                              int requested_rows, const SchemaCapacityInfo& capacity_info) {
  const TableCapacityInfo* table_capacity = capacity_info.find_table(table_name);
  if (table_capacity == nullptr || !table_capacity->static_max_rows.has_value()) {
    return;
  }

  validate_capacity_limit(validation_result, table_name, requested_rows,
                          table_capacity->static_max_rows.value());
}

}  // namespace

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
                                                          const GenerationConfig& config,
                                                          const SchemaCapacityInfo& capacity_info) {
  ValidationResult validation_result(true, {});

  for (const auto& table : tables) {
    const int table_rows = config.get_row_count(table.get_table_name());
    if (table_rows < 0) {
      validation_result.is_valid = false;
      validation_result.errors.push_back("Row count cannot be negative for table '" +
                                         table.get_table_name() + "'");
    }

    validate_static_capacity(validation_result, table.get_table_name(), table_rows, capacity_info);

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
    }

    for (const auto& foreign_key : table.get_foreign_keys()) {
      const int referenced_rows = config.get_row_count(foreign_key.referenced_table);
      if (foreign_key.local_columns.size() != 1 || foreign_key.referenced_columns.size() != 1) {
        validation_result.is_valid = false;
        validation_result.errors.push_back(
            "Composite foreign key generation is not supported for v0.1 in table '" +
            table.get_table_name() + "'");
        continue;
      }

      if (table_rows > 0 && referenced_rows <= 0) {
        validation_result.is_valid = false;
        validation_result.errors.push_back(
            "Cannot generate " + table.get_table_name() + ": it references table '" +
            foreign_key.referenced_table + "', but that table has 0 rows");
      }

      const Table* referenced_table = find_table(tables, foreign_key.referenced_table);
      if (referenced_table != nullptr) {
        std::vector<Column> referenced_columns;
        const Column* referenced_column =
            find_column(*referenced_table, foreign_key.referenced_columns.front(),
                        referenced_columns);

        if (referenced_column != nullptr &&
            !is_integer_type(referenced_column->get_column_type().data_type)) {
          validation_result.is_valid = false;
          validation_result.errors.push_back(
              "Referenced foreign key column '" + foreign_key.referenced_table + "." +
              foreign_key.referenced_columns.front() +
              "' must use INT or BIGINT for v0.1 generation");
        }
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

        if (column != columns.end()) {
          if (column_has_constraint(table, *column, ConstraintType::Unique)) {
            const RowCapacityLimit capacity_limit =
                unique_foreign_key_capacity(table, *column, foreign_key, referenced_rows);
            validate_capacity_limit(validation_result, table.get_table_name(), table_rows,
                                    capacity_limit);
          }
        }
      }
    }
  }

  return validation_result;
}

ValidationResult GenerationFeasibilityValidator::validate(const std::vector<Table>& tables,
                                                          const GenerationConfig& config) {
  const SchemaCapacityInfo capacity_info = CapacityAnalyzer::analyze(tables);
  return validate(tables, config, capacity_info);
}

}  // namespace schemaforge
