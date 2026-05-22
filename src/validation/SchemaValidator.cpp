#include "schemaforge/validation/SchemaValidator.h"

namespace schemaforge {

std::pair<bool, std::string>
SchemaValidator::check_foreign_keys(const std::vector<ForeignKey> &foreign_keys,
                                    const std::vector<Table> &tables) {
  for (const auto &foreign_key : foreign_keys) {
    auto referenced_table_it = std::ranges::find_if(
        tables.begin(), tables.end(), [&foreign_key](const Table &table) {
          return table.get_table_name() == foreign_key.referenced_table;
        });

    if (referenced_table_it == tables.end()) {
      return {false, "Referenced table '" + foreign_key.referenced_table +
                         "' not found for foreign key referencing '" +
                         foreign_key.referenced_table + "'"};
    }

    const auto &referenced_table = *referenced_table_it;

    for (const auto &referenced_column : foreign_key.referenced_columns) {
      std::vector<Column> columns = referenced_table.get_columns();
      auto referenced_column_it = std::ranges::find_if(
          columns.begin(), columns.end(),
          [&referenced_column](const Column &column) {
            return column.get_column_name() == referenced_column;
          });

      if (referenced_column_it == columns.end()) {
        return {false, "Referenced column '" + referenced_column +
                           "' not found in table '" +
                           foreign_key.referenced_table + "' for foreign key"};
      }
    }
  }
  return {true, "All foreign keys are valid"};
}

ValidationResult SchemaValidator::validate(const std::vector<Table> &tables) {
  ValidationResult validation_result;
  for (auto &table : tables) {
    if (table.get_foreign_keys().size() == 0)
      continue;
    auto result = check_foreign_keys(table.get_foreign_keys(), tables);

    if (!result.first) {
      validation_result.isValid = false;
      validation_result.errors.push_back(result.second);
      continue;
    }
  }
  return validation_result;
}

std::ostream &operator<<(std::ostream &os,
                         const ValidationResult &validation_result) {
  os << "Validation Result: "
     << (validation_result.isValid ? "Valid" : "Invalid") << "\n";
  if (!validation_result.isValid) {
    os << "Errors:\n";
    for (const auto &error : validation_result.errors) {
      os << "- " << error << "\n";
    }
  }
  return os;
}
} // namespace schemaforge
