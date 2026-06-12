#include "schemaforge/validation/ValidationRunner.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "schemaforge/domain/ColumnDomainResolver.h"
#include "schemaforge/validation/SQLiteValidator.h"

namespace schemaforge {
namespace {

using SchemaCheck = std::function<void(const std::vector<TablePtr>&, ValidationResult&)>;
using ConfigCheck =
    std::function<void(const std::vector<TablePtr>&, const GenerationConfig&, ValidationResult&)>;
using GenerationCheck = std::function<void(const std::vector<TablePtr>&, const GenerationConfig&,
                                           const SchemaCapacityInfo&, ValidationResult&)>;

bool is_supported_generation_type(DataType data_type) {
  return data_type == DataType::INT || data_type == DataType::BIGINT ||
         data_type == DataType::SMALLINT || data_type == DataType::TEXT ||
         data_type == DataType::VARCHAR || data_type == DataType::CHAR ||
         data_type == DataType::DECIMAL || data_type == DataType::FLOAT ||
         data_type == DataType::DOUBLE || data_type == DataType::REAL ||
         data_type == DataType::BOOLEAN || data_type == DataType::DATE ||
         data_type == DataType::DATETIME || data_type == DataType::TIME;
}

std::string uppercase(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char character) {
    return static_cast<char>(std::toupper(character));
  });
  return value;
}

std::string trim(const std::string& value) {
  const auto begin = std::ranges::find_if(value, [](unsigned char character) {
    return std::isspace(character) == 0;
  });
  if (begin == value.end()) {
    return "";
  }

  const auto end = std::find_if(value.rbegin(), value.rend(), [](unsigned char character) {
    return std::isspace(character) == 0;
  }).base();
  return std::string(begin, end);
}

std::vector<std::string> split_table_body_columns(const std::string& table_body) {
  std::vector<std::string> definitions;
  std::string current;
  int parenthesis_depth = 0;

  for (const char character : table_body) {
    if (character == '(') {
      parenthesis_depth++;
    } else if (character == ')' && parenthesis_depth > 0) {
      parenthesis_depth--;
    }

    if (character == ',' && parenthesis_depth == 0) {
      const std::string definition = trim(current);
      if (!definition.empty()) {
        definitions.push_back(definition);
      }
      current.clear();
      continue;
    }

    current += character;
  }

  const std::string definition = trim(current);
  if (!definition.empty()) {
    definitions.push_back(definition);
  }

  return definitions;
}

std::string word_at(const std::string& value, int index) {
  std::istringstream input(value);
  std::string word;
  for (int current = 0; current <= index; ++current) {
    if (!(input >> word)) {
      return "";
    }
  }
  return word;
}

std::string normalize_type_name(std::string type_name) {
  type_name = uppercase(std::move(type_name));
  const std::size_t parenthesis = type_name.find('(');
  if (parenthesis != std::string::npos) {
    type_name = type_name.substr(0, parenthesis);
  }
  return type_name;
}

void check_known_unsupported_types(const std::string& sql, ValidationResult& validation_result) {
  static const std::unordered_set<std::string> unsupported_types = {
      "JSON", "UUID", "ARRAY", "BLOB", "ENUM", "INET"};

  const std::regex create_table_pattern(
      R"(CREATE\s+TABLE\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([\s\S]*?)\)\s*;)",
      std::regex_constants::icase);

  const auto table_end = std::sregex_iterator();
  for (auto table_it = std::sregex_iterator(sql.begin(), sql.end(), create_table_pattern);
       table_it != table_end; ++table_it) {
    const std::string table_name = (*table_it)[1].str();
    const std::string table_body = (*table_it)[2].str();

    for (const auto& definition : split_table_body_columns(table_body)) {
      const std::string first_word = uppercase(word_at(definition, 0));
      if (first_word == "PRIMARY" || first_word == "FOREIGN" || first_word == "UNIQUE" ||
          first_word == "CONSTRAINT" || first_word == "CHECK") {
        continue;
      }

      const std::string column_name = word_at(definition, 0);
      const std::string type_name = normalize_type_name(word_at(definition, 1));
      if (unsupported_types.contains(type_name)) {
        validation_result.errors.push_back("Unsupported generation type " + type_name +
                                           " for column " + table_name + "." + column_name);
      }
    }
  }
}

const Table* find_table(const std::vector<TablePtr>& tables, const std::string& table_name) {
  const auto table_it =
      std::ranges::find_if(tables, [&table_name](const TablePtr& table_ptr) {
        return table_ptr->get_table_name() == table_name;
      });
  if (table_it == tables.end()) {
    return nullptr;
  }
  return table_it->get();
}

const Column* find_column(const Table* table, const std::string& column_name) {
  if (table == nullptr) {
    return nullptr;
  }

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

bool contains_column(const std::vector<Column*>& columns, const Column* column) {
  return std::ranges::find(columns, column) != columns.end();
}

std::vector<const Column*> primary_key_columns(const Table* table) {
  if (table == nullptr) {
    return {};
  }

  for (const auto& constraint : table->get_table_constraints()) {
    if (constraint.type != ConstraintType::PrimaryKey) {
      continue;
    }

    std::vector<const Column*> columns;
    columns.reserve(constraint.columns.size());
    for (const Column* column : constraint.columns) {
      if (column != nullptr) {
        columns.push_back(column);
      }
    }
    return columns;
  }
  return {};
}

std::vector<const Column*> referenced_columns_for_spec(const ForeignKeySpec& foreign_key_spec,
                                                       const Table* referenced_table) {
  std::vector<const Column*> columns;
  if (referenced_table == nullptr) {
    return columns;
  }

  if (foreign_key_spec.referenced_columns.empty()) {
    return primary_key_columns(referenced_table);
  }

  columns.reserve(foreign_key_spec.referenced_columns.size());
  for (const auto& column_name : foreign_key_spec.referenced_columns) {
    const Column* column = find_column(referenced_table, column_name);
    if (column != nullptr) {
      columns.push_back(column);
    }
  }
  return columns;
}

bool constraint_columns_match(const TableConstraint& constraint,
                              const std::vector<const Column*>& columns) {
  if (constraint.columns.size() != columns.size()) {
    return false;
  }

  return std::ranges::all_of(columns, [&constraint](const Column* column) {
    return column != nullptr && contains_column(constraint.columns, column);
  });
}

bool has_matching_key_constraint(const Table* table, const std::vector<const Column*>& columns) {
  if (table == nullptr || columns.empty()) {
    return false;
  }

  for (const auto& constraint : table->get_table_constraints()) {
    if (constraint.type != ConstraintType::PrimaryKey && constraint.type != ConstraintType::Unique) {
      continue;
    }
    if (constraint_columns_match(constraint, columns)) {
      return true;
    }
  }
  return false;
}

std::string column_name_or_unknown(const Column* column) {
  return column == nullptr ? "<unknown>" : column->get_column_name();
}

void check_duplicate_table_names(const std::vector<TablePtr>& tables,
                                 ValidationResult& validation_result) {
  std::unordered_set<std::string> seen;
  std::unordered_set<std::string> reported;
  for (const auto& table_ptr : tables) {
    const std::string table_name = table_ptr->get_table_name();
    if (!seen.insert(table_name).second && reported.insert(table_name).second) {
      validation_result.errors.push_back("Duplicate table name '" + table_name + "'");
    }
  }
}

void check_duplicate_column_names(const std::vector<TablePtr>& tables,
                                  ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    std::unordered_set<std::string> seen;
    std::unordered_set<std::string> reported;
    for (const auto& column_ptr : table_ptr->get_columns()) {
      const std::string column_name = column_ptr->get_column_name();
      if (!seen.insert(column_name).second && reported.insert(column_name).second) {
        validation_result.errors.push_back("Duplicate column name '" + table_ptr->get_table_name() +
                                           "." + column_name + "'");
      }
    }
  }
}

void check_primary_key_columns_exist(const std::vector<TablePtr>& tables,
                                     ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    for (const auto& constraint : table_ptr->get_table_constraints()) {
      if (constraint.type != ConstraintType::PrimaryKey) {
        continue;
      }
      for (const Column* column : constraint.columns) {
        if (column == nullptr) {
          validation_result.errors.push_back("Primary key on table '" +
                                             table_ptr->get_table_name() +
                                             "' references an unknown column");
        }
      }
    }
  }
}

void check_unique_constraint_columns_exist(const std::vector<TablePtr>& tables,
                                           ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    for (const auto& constraint : table_ptr->get_table_constraints()) {
      if (constraint.type != ConstraintType::Unique) {
        continue;
      }
      for (const Column* column : constraint.columns) {
        if (column == nullptr) {
          validation_result.errors.push_back("Unique constraint on table '" +
                                             table_ptr->get_table_name() +
                                             "' references an unknown column");
        }
      }
    }
  }
}

void check_fk_local_columns_exist(const std::vector<TablePtr>& tables,
                                  ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    for (const auto& foreign_key_spec : table_ptr->get_foreign_key_specs()) {
      for (const auto& local_column_name : foreign_key_spec.local_columns) {
        if (find_column(table_ptr.get(), local_column_name) == nullptr) {
          validation_result.errors.push_back("Foreign key on table '" +
                                             table_ptr->get_table_name() +
                                             "' references unknown local column '" +
                                             local_column_name + "'");
        }
      }
    }
  }
}

void check_fk_referenced_table_exists(const std::vector<TablePtr>& tables,
                                      ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    for (const auto& foreign_key_spec : table_ptr->get_foreign_key_specs()) {
      if (find_table(tables, foreign_key_spec.referenced_table) == nullptr) {
        validation_result.errors.push_back("Referenced table '" +
                                           foreign_key_spec.referenced_table +
                                           "' not found for foreign key on table '" +
                                           table_ptr->get_table_name() + "'");
      }
    }
  }
}

void check_fk_referenced_columns_exist(const std::vector<TablePtr>& tables,
                                       ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    for (const auto& foreign_key_spec : table_ptr->get_foreign_key_specs()) {
      const Table* referenced_table = find_table(tables, foreign_key_spec.referenced_table);
      if (referenced_table == nullptr) {
        continue;
      }

      if (foreign_key_spec.referenced_columns.empty()) {
        if (primary_key_columns(referenced_table).empty()) {
          validation_result.errors.push_back("Foreign key on table '" +
                                             table_ptr->get_table_name() + "' references table '" +
                                             referenced_table->get_table_name() +
                                             "' without referenced columns, but that table has no "
                                             "primary key");
        }
        continue;
      }

      for (const auto& referenced_column_name : foreign_key_spec.referenced_columns) {
        if (find_column(referenced_table, referenced_column_name) == nullptr) {
          validation_result.errors.push_back("Referenced column '" + referenced_column_name +
                                             "' not found in table '" +
                                             foreign_key_spec.referenced_table +
                                             "' for foreign key on table '" +
                                             table_ptr->get_table_name() + "'");
        }
      }
    }
  }
}

void check_fk_column_count_matches(const std::vector<TablePtr>& tables,
                                   ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    for (const auto& foreign_key_spec : table_ptr->get_foreign_key_specs()) {
      const Table* referenced_table = find_table(tables, foreign_key_spec.referenced_table);
      if (referenced_table == nullptr) {
        continue;
      }

      const std::size_t referenced_count =
          foreign_key_spec.referenced_columns.empty()
              ? primary_key_columns(referenced_table).size()
              : foreign_key_spec.referenced_columns.size();
      if (foreign_key_spec.local_columns.size() != referenced_count) {
        validation_result.errors.push_back("Foreign key on table '" + table_ptr->get_table_name() +
                                           "' has " +
                                           std::to_string(foreign_key_spec.local_columns.size()) +
                                           " local columns but " +
                                           std::to_string(referenced_count) +
                                           " referenced columns");
      }
    }
  }
}

void check_fk_types_match(const std::vector<TablePtr>& tables, ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    for (const auto& foreign_key_spec : table_ptr->get_foreign_key_specs()) {
      const Table* referenced_table = find_table(tables, foreign_key_spec.referenced_table);
      if (referenced_table == nullptr) {
        continue;
      }

      std::vector<const Column*> local_columns;
      local_columns.reserve(foreign_key_spec.local_columns.size());
      for (const auto& local_column_name : foreign_key_spec.local_columns) {
        const Column* local_column = find_column(table_ptr.get(), local_column_name);
        if (local_column != nullptr) {
          local_columns.push_back(local_column);
        }
      }

      const auto referenced_columns = referenced_columns_for_spec(foreign_key_spec, referenced_table);
      if (local_columns.size() != referenced_columns.size()) {
        continue;
      }

      for (std::size_t index = 0; index < local_columns.size(); ++index) {
        const Column* local_column = local_columns[index];
        const Column* referenced_column = referenced_columns[index];
        if (local_column->get_column_type().data_type !=
            referenced_column->get_column_type().data_type) {
          validation_result.errors.push_back(
              "Foreign key column '" + table_ptr->get_table_name() + "." +
              local_column->get_column_name() + "' type does not match referenced column '" +
              referenced_table->get_table_name() + "." + referenced_column->get_column_name() +
              "'");
        }
      }
    }
  }
}

void check_fk_references_key(const std::vector<TablePtr>& tables, ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    for (const auto& foreign_key_spec : table_ptr->get_foreign_key_specs()) {
      const Table* referenced_table = find_table(tables, foreign_key_spec.referenced_table);
      if (referenced_table == nullptr) {
        continue;
      }

      const auto referenced_columns = referenced_columns_for_spec(foreign_key_spec, referenced_table);
      if (!has_matching_key_constraint(referenced_table, referenced_columns)) {
        validation_result.errors.push_back("Foreign key on table '" + table_ptr->get_table_name() +
                                           "' must reference a PRIMARY KEY or UNIQUE constraint on "
                                           "table '" +
                                           referenced_table->get_table_name() + "'");
      }
    }
  }
}

void check_cycle_detection(const std::vector<TablePtr>& tables, ValidationResult& validation_result) {
  std::unordered_set<std::string> table_names;
  for (const auto& table_ptr : tables) {
    table_names.insert(table_ptr->get_table_name());
  }

  std::unordered_map<std::string, std::vector<std::string>> dependencies;
  for (const auto& table_ptr : tables) {
    const auto table_name = table_ptr->get_table_name();
    for (const auto& foreign_key_spec : table_ptr->get_foreign_key_specs()) {
      if (foreign_key_spec.referenced_table == table_name ||
          !table_names.contains(foreign_key_spec.referenced_table)) {
        continue;
      }
      dependencies[table_name].push_back(foreign_key_spec.referenced_table);
    }
  }

  std::unordered_set<std::string> visiting;
  std::unordered_set<std::string> visited;
  bool has_cycle = false;

  std::function<void(const std::string&)> visit = [&](const std::string& table_name) {
    if (has_cycle || visited.contains(table_name)) {
      return;
    }
    if (visiting.contains(table_name)) {
      has_cycle = true;
      return;
    }

    visiting.insert(table_name);
    for (const auto& dependency : dependencies[table_name]) {
      visit(dependency);
    }
    visiting.erase(table_name);
    visited.insert(table_name);
  };

  for (const auto& table_name : table_names) {
    visit(table_name);
  }

  if (has_cycle) {
    validation_result.errors.push_back("Cycle detected. Tables cannot be fully topologically sorted.");
  }
}

void check_self_reference_unsupported(const std::vector<TablePtr>& tables,
                                      ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    for (const auto& foreign_key_spec : table_ptr->get_foreign_key_specs()) {
      if (foreign_key_spec.referenced_table == table_ptr->get_table_name()) {
        validation_result.errors.push_back("Self-referencing foreign key on table '" +
                                           table_ptr->get_table_name() +
                                           "' is not supported");
      }
    }
  }
}

void check_unsupported_generation_type(const std::vector<TablePtr>& tables,
                                       ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    for (const auto& column_ptr : table_ptr->get_columns()) {
      if (!is_supported_generation_type(column_ptr->get_column_type().data_type)) {
        validation_result.errors.push_back("Column '" + table_ptr->get_table_name() + "." +
                                           column_ptr->get_column_name() +
                                           "' uses an unsupported generation type");
      }
    }
  }
}

void check_unsupported_check_constraints(const std::vector<TablePtr>& tables,
                                         ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    for (const auto& check : table_ptr->get_check_constraints()) {
      if (check.type == CheckConstraintType::Unsupported || check.column == nullptr) {
        const std::string column_name =
            check.column_name.empty() ? "<unknown>" : check.column_name;
        validation_result.errors.push_back("Unsupported CHECK constraint on " +
                                           table_ptr->get_table_name() + "." + column_name +
                                           ": " + check.raw_sql);
      }
    }
  }
}

void check_composite_primary_key_unsupported(const std::vector<TablePtr>& tables,
                                             ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    for (const auto& constraint : table_ptr->get_table_constraints()) {
      if (constraint.type == ConstraintType::PrimaryKey && constraint.columns.size() > 1) {
        validation_result.errors.push_back("Composite primary keys are not supported yet");
      }
    }
  }
}

void check_unsupported_pk_fk_generation_type(const std::vector<TablePtr>& tables,
                                             ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    for (const auto& constraint : table_ptr->get_table_constraints()) {
      if (constraint.type != ConstraintType::PrimaryKey) {
        continue;
      }
      for (const Column* column : constraint.columns) {
        if (column != nullptr &&
            !ColumnDomainResolver::is_integer_type(column->get_column_type().data_type) &&
            !ColumnDomainResolver::is_text_type(column->get_column_type().data_type) &&
            column->get_column_type().data_type != DataType::CHAR) {
          validation_result.errors.push_back("Primary key column '" +
                                             table_ptr->get_table_name() + "." +
                                             column->get_column_name() +
                                             "' must use INT, BIGINT, SMALLINT, TEXT, VARCHAR, or "
                                             "CHAR for generation");
        }
      }
    }

    for (const auto& foreign_key_spec : table_ptr->get_foreign_key_specs()) {
      for (const auto& local_column_name : foreign_key_spec.local_columns) {
        const Column* local_column = find_column(table_ptr.get(), local_column_name);
        if (local_column != nullptr &&
            !ColumnDomainResolver::is_integer_type(local_column->get_column_type().data_type) &&
            !ColumnDomainResolver::is_text_type(local_column->get_column_type().data_type)) {
          validation_result.errors.push_back("Foreign key column '" +
                                             table_ptr->get_table_name() + "." +
                                             local_column->get_column_name() +
                                             "' must use INT, BIGINT, SMALLINT, TEXT, or VARCHAR "
                                             "for generation");
        }
      }
    }
  }
}

void check_config_unknown_table(const std::vector<TablePtr>& tables, const GenerationConfig& config,
                                ValidationResult& validation_result) {
  std::unordered_set<std::string> table_names;
  for (const auto& table_ptr : tables) {
    table_names.insert(table_ptr->get_table_name());
  }

  for (const auto& [table_name, row_count] : config.table_row_counts) {
    (void)row_count;
    if (!table_names.contains(table_name)) {
      validation_result.errors.push_back("Config contains unknown table '" + table_name + "'");
    }
  }
}

void check_parent_rows_for_fk(const std::vector<TablePtr>& tables, const GenerationConfig& config,
                              const SchemaCapacityInfo&, ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    const int child_rows = config.get_row_count(table_ptr->get_table_name());
    for (const auto& foreign_key_spec : table_ptr->get_foreign_key_specs()) {
      const Table* parent_table = find_table(tables, foreign_key_spec.referenced_table);
      if (parent_table == nullptr) {
        continue;
      }

      const int parent_rows = config.get_row_count(parent_table->get_table_name());
      if (child_rows > 0 && parent_rows <= 0) {
        validation_result.errors.push_back("Cannot generate " + table_ptr->get_table_name() +
                                           ": it references table '" +
                                           parent_table->get_table_name() +
                                           "', but that table has 0 rows");
      }
    }
  }
}

void check_unique_boolean_capacity(const std::vector<TablePtr>&, const GenerationConfig&,
                                   const SchemaCapacityInfo& capacity_info,
                                   ValidationResult& validation_result) {
  for (const auto& table_info : capacity_info.tables) {
    if (table_info.requested_rows <= table_info.max_rows) {
      continue;
    }

    const auto reason_it = std::ranges::find_if(table_info.reasons, [](const std::string& reason) {
      return reason.find("UNIQUE BOOLEAN") != std::string::npos;
    });
    if (reason_it == table_info.reasons.end()) {
      continue;
    }

    validation_result.errors.push_back("Cannot generate " +
                                       std::to_string(table_info.requested_rows) +
                                       " rows for table '" + table_info.table_id +
                                       "': max rows is " +
                                       std::to_string(table_info.max_rows) + " because " +
                                       *reason_it);
  }
}

std::string limited_column_from_reason(const std::string& reason) {
  const std::string prefix = "Column ";
  const std::string marker = " is ";
  if (!reason.starts_with(prefix)) {
    return "";
  }

  const std::size_t marker_position = reason.find(marker, prefix.size());
  if (marker_position == std::string::npos) {
    return "";
  }

  return reason.substr(prefix.size(), marker_position - prefix.size());
}

void check_char_capacity(const std::vector<TablePtr>&, const GenerationConfig&,
                         const SchemaCapacityInfo& capacity_info,
                         ValidationResult& validation_result) {
  for (const auto& table_info : capacity_info.tables) {
    if (table_info.requested_rows <= table_info.max_rows) {
      continue;
    }

    const auto reason_it = std::ranges::find_if(table_info.reasons, [](const std::string& reason) {
      return reason.find(" CHAR(") != std::string::npos;
    });
    if (reason_it == table_info.reasons.end()) {
      continue;
    }

    const std::string column_name = limited_column_from_reason(*reason_it);
    validation_result.errors.push_back("Cannot generate " +
                                       std::to_string(table_info.requested_rows) + " rows for " +
                                       (column_name.empty() ? table_info.table_id : column_name) +
                                       ". " + *reason_it);
  }
}

void check_unique_check_capacity(const std::vector<TablePtr>&, const GenerationConfig&,
                                 const SchemaCapacityInfo& capacity_info,
                                 ValidationResult& validation_result) {
  for (const auto& table_info : capacity_info.tables) {
    if (table_info.requested_rows <= table_info.max_rows) {
      continue;
    }

    const auto reason_it = std::ranges::find_if(table_info.reasons, [](const std::string& reason) {
      return reason.find("UNIQUE CHECK") != std::string::npos;
    });
    if (reason_it == table_info.reasons.end()) {
      continue;
    }

    const std::string column_name = limited_column_from_reason(*reason_it);
    validation_result.errors.push_back("Cannot generate " +
                                       std::to_string(table_info.requested_rows) + " rows for " +
                                       (column_name.empty() ? table_info.table_id : column_name) +
                                       ". " + *reason_it);
  }
}

void check_composite_unique_capacity(const std::vector<TablePtr>&, const GenerationConfig&,
                                     const SchemaCapacityInfo& capacity_info,
                                     ValidationResult& validation_result) {
  for (const auto& table_info : capacity_info.tables) {
    if (table_info.requested_rows <= table_info.max_rows) {
      continue;
    }

    const auto reason_it = std::ranges::find_if(table_info.reasons, [](const std::string& reason) {
      return reason.find("Composite UNIQUE(") != std::string::npos;
    });
    if (reason_it == table_info.reasons.end()) {
      continue;
    }

    validation_result.errors.push_back("Cannot generate " +
                                       std::to_string(table_info.requested_rows) +
                                       " rows for table '" + table_info.table_id + "': " +
                                       *reason_it);
  }
}

void check_unique_fk_capacity(const std::vector<TablePtr>& tables, const GenerationConfig& config,
                              const SchemaCapacityInfo&, ValidationResult& validation_result) {
  for (const auto& table_ptr : tables) {
    const int child_rows = config.get_row_count(table_ptr->get_table_name());
    for (const auto& foreign_key : table_ptr->get_foreign_keys()) {
      const Table* parent_table = foreign_key.referenced_table;
      if (parent_table == nullptr || !has_matching_key_constraint(table_ptr.get(), [&] {
            std::vector<const Column*> columns;
            columns.reserve(foreign_key.local_columns.size());
            for (const Column* column : foreign_key.local_columns) {
              columns.push_back(column);
            }
            return columns;
          }())) {
        continue;
      }

      const int parent_rows = config.get_row_count(parent_table->get_table_name());
      if (child_rows > parent_rows) {
        validation_result.errors.push_back("Cannot generate " + std::to_string(child_rows) +
                                           " rows for table '" + table_ptr->get_table_name() +
                                           "': max rows is " + std::to_string(parent_rows) +
                                           " because UNIQUE foreign key on table '" +
                                           table_ptr->get_table_name() + "' references table '" +
                                           parent_table->get_table_name() + "' with " +
                                           std::to_string(parent_rows) + " available rows");
      }
    }
  }
}

}  // namespace

ValidationResult ValidationRunner::validate_schema_file(const std::string& schema_path) {
  ValidationResult validation_result(true, {});
  const std::vector<std::function<void(ValidationResult&)>> checks = {
      [&schema_path](ValidationResult& result) {
        if (schema_path.empty()) {
          result.errors.push_back("Missing schema path");
        }
      },
      [&schema_path](ValidationResult& result) {
        if (schema_path.empty()) {
          return;
        }

        std::ifstream schema_file(schema_path);
        if (!schema_file.is_open()) {
          result.errors.push_back("Missing schema file: " + schema_path);
          return;
        }

        std::stringstream buffer;
        buffer << schema_file.rdbuf();
        check_known_unsupported_types(buffer.str(), result);
      },
  };

  for (const auto& check : checks) {
    check(validation_result);
  }
  validation_result.is_valid = validation_result.errors.empty();
  return validation_result;
}

ValidationResult ValidationRunner::validate_schema(const std::vector<TablePtr>& tables) {
  ValidationResult validation_result(true, {});
  const std::vector<SchemaCheck> checks = {
      check_duplicate_table_names,
      check_duplicate_column_names,
      check_primary_key_columns_exist,
      check_unique_constraint_columns_exist,
      check_fk_local_columns_exist,
      check_fk_referenced_table_exists,
      check_fk_referenced_columns_exist,
      check_fk_column_count_matches,
      check_fk_types_match,
      check_fk_references_key,
      check_cycle_detection,
      check_self_reference_unsupported,
      check_unsupported_generation_type,
      check_unsupported_check_constraints,
      check_composite_primary_key_unsupported,
      check_unsupported_pk_fk_generation_type,
  };

  for (const auto& check : checks) {
    check(tables, validation_result);
  }
  validation_result.is_valid = validation_result.errors.empty();
  return validation_result;
}

ValidationResult ValidationRunner::validate_config(const std::vector<TablePtr>& tables,
                                                   const GenerationConfig& config) {
  ValidationResult validation_result(true, {});
  const std::vector<ConfigCheck> checks = {
      check_config_unknown_table,
  };

  for (const auto& check : checks) {
    check(tables, config, validation_result);
  }
  validation_result.is_valid = validation_result.errors.empty();
  return validation_result;
}

ValidationResult ValidationRunner::validate_generation(const std::vector<TablePtr>& tables,
                                                       const GenerationConfig& config,
                                                       const SchemaCapacityInfo& capacity_info) {
  ValidationResult validation_result(true, {});
  const std::vector<GenerationCheck> checks = {
      check_parent_rows_for_fk,
      check_unique_boolean_capacity,
      check_char_capacity,
      check_unique_check_capacity,
      check_composite_unique_capacity,
      check_unique_fk_capacity,
  };

  for (const auto& check : checks) {
    check(tables, config, capacity_info, validation_result);
  }
  validation_result.is_valid = validation_result.errors.empty();
  return validation_result;
}

ValidationResult ValidationRunner::validate_sqlite(
    const std::string& schema_sql, const std::vector<std::string>& insert_statements) {
  return SQLiteValidator::validate(schema_sql, insert_statements);
}

}  // namespace schemaforge
