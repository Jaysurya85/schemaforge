#include "schemaforge/validation/SchemaValidator.h"

namespace schemaforge {

bool SchemaValidator::is_supported_generation_type(DataType data_type) {
  return data_type == DataType::INT || data_type == DataType::BIGINT ||
         data_type == DataType::TEXT || data_type == DataType::VARCHAR ||
         data_type == DataType::DECIMAL || data_type == DataType::FLOAT ||
         data_type == DataType::DOUBLE || data_type == DataType::REAL ||
         data_type == DataType::BOOLEAN;
}

bool SchemaValidator::is_integer_type(DataType data_type) {
  return data_type == DataType::INT || data_type == DataType::BIGINT;
}

const Table* SchemaValidator::find_table(const std::vector<TablePtr>& tables,
                                         const std::string& table_name) {
  const auto table_it =
      std::ranges::find_if(tables, [&table_name](const TablePtr& table_ptr) {
        return table_ptr->get_table_name() == table_name;
      });
  if (table_it == tables.end()) {
    return nullptr;
  }
  return table_it->get();
}

const Column* SchemaValidator::find_column(const Table* table, const std::string& column_name) {
  const auto& columns = table->get_columns();
  const auto column_it =
      std::ranges::find_if(columns, [&column_name](const ColumnPtr& column_ptr) {
        return column_ptr->get_column_name() == column_name;
      });
  if (column_it == columns.end()) {
    return nullptr;
  }
  return column_it->get();
}

const Column* SchemaValidator::primary_key_column(const Table* table) {
  for (const auto& constraint : table->get_table_constraints()) {
    if (constraint.type == ConstraintType::PrimaryKey && constraint.columns.size() == 1) {
      return constraint.columns.front();
    }
  }
  return nullptr;
}

void SchemaValidator::check_supported_generation_types(ValidationResult& validation_result,
                                                       const Table& table) {
  for (const auto& column_ptr : table.get_columns()) {
    if (!is_supported_generation_type(column_ptr->get_column_type().data_type)) {
      validation_result.is_valid = false;
      validation_result.errors.push_back("Column '" + table.get_table_name() + "." +
                                         column_ptr->get_column_name() +
                                         "' uses an unsupported generation type");
    }
  }
}

std::pair<bool, std::string> SchemaValidator::check_foreign_keys_specs(
    const std::vector<ForeignKeySpec>& foreign_keys_specs, const std::vector<TablePtr>& tables) {
  for (const auto& foreign_key_spec : foreign_keys_specs) {
    const Table* referenced_table = find_table(tables, foreign_key_spec.referenced_table);
    if (referenced_table == nullptr) {
      return {false, "Referenced table '" + foreign_key_spec.referenced_table +
                         "' not found for foreign key referencing '" +
                         foreign_key_spec.referenced_table + "'"};
    }

    std::vector<const Column*> referenced_columns;
    referenced_columns.reserve(foreign_key_spec.referenced_columns.size());

    for (const auto& referenced_column_name : foreign_key_spec.referenced_columns) {
      const Column* referenced_column = find_column(referenced_table, referenced_column_name);
      if (referenced_column == nullptr) {
        return {false, "Referenced column '" + referenced_column_name + "' not found in table '" +
                           foreign_key_spec.referenced_table + "' for foreign key"};
      }
      referenced_columns.push_back(referenced_column);
    }

    if (referenced_columns.empty()) {
      const Column* primary_key = primary_key_column(referenced_table);
      if (primary_key != nullptr) {
        referenced_columns.push_back(primary_key);
      }
    }

    for (const Column* referenced_column : referenced_columns) {
      if (!is_integer_type(referenced_column->get_column_type().data_type)) {
        return {false, "Referenced foreign key column '" +
                           foreign_key_spec.referenced_table + "." +
                           referenced_column->get_column_name() +
                           "' must use INT or BIGINT"};
      }
    }
  }
  return {true, "All foreign keys are valid"};
}

ValidationResult SchemaValidator::validate(const std::vector<TablePtr>& tables) {
  ValidationResult validation_result(true, {});
  for (const auto& table_ptr : tables) {
    auto& table = *table_ptr;
    check_supported_generation_types(validation_result, table);

    if (table.get_foreign_key_specs().size() == 0) {
      continue;
    }
    auto result = check_foreign_keys_specs(table.get_foreign_key_specs(), tables);

    if (!result.first) {
      validation_result.is_valid = false;
      validation_result.errors.push_back(result.second);
      continue;
    }
  }
  return validation_result;
}

std::ostream& operator<<(std::ostream& os, const ValidationResult& validation_result) {
  os << "Validation Result: " << (validation_result.is_valid ? "Valid" : "Invalid") << "\n";
  if (!validation_result.is_valid) {
    os << "Errors:\n";
    for (const auto& error : validation_result.errors) {
      os << "- " << error << "\n";
    }
  }
  return os;
}
}  // namespace schemaforge
