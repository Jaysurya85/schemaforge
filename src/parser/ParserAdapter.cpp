#include "schemaforge/parser/ParserAdapter.h"

#include <cctype>

#include "SQLParser.h"

namespace schemaforge {

DataType ParserAdapter::convert_data_type(hsql::DataType data_type) {
  switch (data_type) {
    case hsql::DataType::BIGINT:
      return DataType::BIGINT;
    case hsql::DataType::BOOLEAN:
      return DataType::BOOLEAN;
    case hsql::DataType::CHAR:
      return DataType::CHAR;
    case hsql::DataType::DATE:
      return DataType::DATE;
    case hsql::DataType::DATETIME:
      return DataType::DATETIME;
    case hsql::DataType::DECIMAL:
      return DataType::DECIMAL;
    case hsql::DataType::DOUBLE:
      return DataType::DOUBLE;
    case hsql::DataType::FLOAT:
      return DataType::FLOAT;
    case hsql::DataType::INT:
      return DataType::INT;
    case hsql::DataType::LONG:
      return DataType::LONG;
    case hsql::DataType::REAL:
      return DataType::REAL;
    case hsql::DataType::SMALLINT:
      return DataType::SMALLINT;
    case hsql::DataType::TEXT:
      return DataType::TEXT;
    case hsql::DataType::TIME:
      return DataType::TIME;
    case hsql::DataType::VARCHAR:
      return DataType::VARCHAR;
    default:
      return DataType::UNKNOWN;
  }
}

ColumnType ParserAdapter::convert_column_type(const hsql::ColumnType& hsql_column_type) {
  return ColumnType{.data_type = convert_data_type(hsql_column_type.data_type),
                    .length = hsql_column_type.length,
                    .precision = hsql_column_type.precision,
                    .scale = hsql_column_type.scale};
}

ConstraintType ParserAdapter::convert_constraint_type(const hsql::ConstraintType& constraint_type) {
  switch (constraint_type) {
    case hsql::ConstraintType::PrimaryKey:
      return ConstraintType::PrimaryKey;
    case hsql::ConstraintType::ForeignKey:
      return ConstraintType::ForeignKey;
    case hsql::ConstraintType::NotNull:
      return ConstraintType::NotNull;
    case hsql::ConstraintType::Null:
      return ConstraintType::Null;
    case hsql::ConstraintType::Unique:
      return ConstraintType::Unique;
    default:
      return ConstraintType::Unknown;
  }
}

Column* ParserAdapter::find_column_by_name(const std::string& name,
                                           const std::vector<ColumnPtr>& columns) {
  for (const auto& column : columns) {
    if (column->get_column_name() == name) {
      return column.get();
    }
  }
  return nullptr;
}

Table* ParserAdapter::find_table_by_name(const std::string& name,
                                         const std::vector<TablePtr>& tables) {
  for (const auto& table : tables) {
    if (table->get_table_name() == name) {
      return table.get();
    }
  }
  return nullptr;
}

namespace {

bool is_word_character(char character) {
  return std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_';
}

bool case_insensitive_char_match(char left, char right) {
  return std::toupper(static_cast<unsigned char>(left)) ==
         std::toupper(static_cast<unsigned char>(right));
}

bool case_insensitive_match_at(const std::string& value, std::size_t position,
                               const std::string& target) {
  if (position + target.size() > value.size()) {
    return false;
  }

  for (std::size_t index = 0; index < target.size(); ++index) {
    if (!case_insensitive_char_match(value[position + index], target[index])) {
      return false;
    }
  }
  return true;
}

std::string normalize_bare_char_type(const std::string& sql) {
  std::string normalized;
  normalized.reserve(sql.size());

  for (std::size_t index = 0; index < sql.size();) {
    const bool matches_char = case_insensitive_match_at(sql, index, "CHAR");
    const bool starts_word = index == 0 || !is_word_character(sql[index - 1]);
    const std::size_t after_char = index + 4;
    const bool ends_word = after_char >= sql.size() || !is_word_character(sql[after_char]);

    if (!matches_char || !starts_word || !ends_word) {
      normalized += sql[index++];
      continue;
    }

    std::size_t next = after_char;
    while (next < sql.size() && std::isspace(static_cast<unsigned char>(sql[next])) != 0) {
      next++;
    }

    if (next < sql.size() && sql[next] == '(') {
      normalized.append(sql, index, after_char - index);
      index = after_char;
      continue;
    }

    normalized += "CHAR(1)";
    index = after_char;
  }

  return normalized;
}

std::vector<Column*> primary_key_columns(const Table* table) {
  for (const auto& constraint : table->get_table_constraints()) {
    if (constraint.type == ConstraintType::PrimaryKey) {
      return constraint.columns;
    }
  }
  return {};
}

}  // namespace

TableConstraint ParserAdapter::convert_table_constraint(
    const hsql::TableConstraint* table_constraint, const std::vector<ColumnPtr>& columns) {
  std::vector<Column*> contraint_columns;

  if (table_constraint != nullptr && table_constraint->columnNames != nullptr) {
    for (const auto* column_name : *table_constraint->columnNames) {
      if (column_name != nullptr) {
        contraint_columns.push_back(find_column_by_name(column_name, columns));
      }
    }
  }

  return {convert_constraint_type(table_constraint->type), contraint_columns};
}

bool ParserAdapter::should_store_as_table_constraint(TableConstraint& table_contraint) {
  if (table_contraint.columns.empty()) {
    return false;
  }

  switch (table_contraint.type) {
    case ConstraintType::PrimaryKey:
    case ConstraintType::Unique:
      return true;

    case ConstraintType::Null:
    case ConstraintType::NotNull:
    case ConstraintType::ForeignKey:
    case ConstraintType::Unknown:
    default:
      return false;
  }
}

std::vector<TableConstraint> ParserAdapter::extract_table_constraints(
    const hsql::CreateStatement* create_stmt, const std::vector<ColumnPtr>& columns) {
  std::vector<TableConstraint> table_constraints;

  if (create_stmt == nullptr || create_stmt->tableConstraints == nullptr) {
    return table_constraints;
  }

  for (const auto* constraint : *create_stmt->tableConstraints) {
    TableConstraint table_constraint = convert_table_constraint(constraint, columns);
    if (!should_store_as_table_constraint(table_constraint)) {
      continue;
    }
    table_constraints.push_back(convert_table_constraint(constraint, columns));
  }

  return table_constraints;
}

std::vector<TableConstraint> ParserAdapter::extract_column_constraints(
    const hsql::CreateStatement* create_stmt, const std::vector<ColumnPtr>& columns) {
  std::vector<TableConstraint> column_constraints;

  if (create_stmt == nullptr || create_stmt->columns == nullptr) {
    return column_constraints;
  }

  for (const auto* col : *create_stmt->columns) {
    if (col == nullptr || col->column_constraints == nullptr) {
      continue;
    }

    for (const auto& constraint : *col->column_constraints) {
      TableConstraint column_constraint(
          convert_constraint_type(constraint),
          std::vector<Column*>{find_column_by_name(col->name, columns)});

      if (!should_store_as_table_constraint(column_constraint)) {
        continue;
      }
      column_constraints.push_back(std::move(column_constraint));
    }
  }

  return column_constraints;
}

std::vector<TableConstraint> ParserAdapter::convert_constraints(
    const hsql::CreateStatement* create_stmt, const std::vector<ColumnPtr>& columns) {
  auto constraints = extract_table_constraints(create_stmt, columns);
  auto column_constraints = extract_column_constraints(create_stmt, columns);

  constraints.insert(constraints.end(), std::make_move_iterator(column_constraints.begin()),
                     std::make_move_iterator(column_constraints.end()));

  return constraints;
}

std::vector<std::string> ParserAdapter::convert_names(const std::vector<char*>* names) {
  std::vector<std::string> result;

  if (names == nullptr) {
    return result;
  }

  result.reserve(names->size());

  for (const auto* name : *names) {
    if (name != nullptr) {
      result.emplace_back(name);
    }
  }

  return result;
}

ForeignKeySpec ParserAdapter::convert_foreign_key_spec(
    const hsql::ForeignKeyConstraint* foreign_key_constraint) {
  std::vector<std::string> local_columns;
  std::string referenced_table;
  std::vector<std::string> referenced_columns;

  if (foreign_key_constraint != nullptr) {
    local_columns = convert_names(foreign_key_constraint->columnNames);
    const auto* references = foreign_key_constraint->references;

    if (references != nullptr) {
      if (references->table != nullptr) {
        referenced_table = references->table;
      }

      referenced_columns = convert_names(references->columns);
    }
  }

  return {local_columns, referenced_table, referenced_columns};
}

std::vector<ForeignKeySpec> ParserAdapter::extract_table_foreign_keys_spec(
    const hsql::CreateStatement* create_stmt) {
  std::vector<ForeignKeySpec> foreign_keys_spec;

  if (create_stmt == nullptr || create_stmt->tableConstraints == nullptr) {
    return foreign_keys_spec;
  }

  for (const auto* constraint : *create_stmt->tableConstraints) {
    if (constraint == nullptr || constraint->type != hsql::ConstraintType::ForeignKey) {
      continue;
    }

    const auto* foreign_key_constraint = static_cast<const hsql::ForeignKeyConstraint*>(constraint);

    foreign_keys_spec.push_back(convert_foreign_key_spec(foreign_key_constraint));
  }
  return foreign_keys_spec;
}

std::vector<ForeignKeySpec> ParserAdapter::extract_column_foreign_keys_spec(
    const hsql::CreateStatement* create_stmt) {
  std::vector<ForeignKeySpec> foreign_keys_spec;

  if (create_stmt == nullptr || create_stmt->columns == nullptr) {
    return foreign_keys_spec;
  }

  for (const auto* col : *create_stmt->columns) {
    if (col == nullptr || col->references == nullptr) {
      continue;
    }

    for (const auto* references : *col->references) {
      if (references == nullptr) {
        continue;
      }

      std::vector<std::string> local_columns{col->name};

      std::string referenced_table;
      if (references->table != nullptr) {
        referenced_table = references->table;
      }

      auto referenced_columns = convert_names(references->columns);

      foreign_keys_spec.emplace_back(local_columns, referenced_table, referenced_columns);
    }
  }
  return foreign_keys_spec;
}

std::vector<ForeignKeySpec> ParserAdapter::extract_foreign_keys_spec(
    const hsql::CreateStatement* create_stmt) {
  auto foreign_keys_spec = extract_table_foreign_keys_spec(create_stmt);
  auto column_foreign_keys_spec = extract_column_foreign_keys_spec(create_stmt);

  foreign_keys_spec.insert(foreign_keys_spec.end(),
                           std::make_move_iterator(column_foreign_keys_spec.begin()),
                           std::make_move_iterator(column_foreign_keys_spec.end()));

  return foreign_keys_spec;
}

Column ParserAdapter::convert_column(const hsql::ColumnDefinition* col) {
  ColumnType column_type{convert_column_type(col->type)};
  return {col->name, column_type, col->nullable};
}

std::vector<ColumnPtr> ParserAdapter::convert_columns(const hsql::CreateStatement* create_stmt) {
  std::vector<ColumnPtr> columns;

  if (create_stmt == nullptr || create_stmt->columns == nullptr) {
    return columns;
  }

  for (const auto* col : *create_stmt->columns) {
    if (col == nullptr) {
      continue;
    }

    columns.push_back(std::make_unique<Column>(convert_column(col)));
  }

  return columns;
}

Table ParserAdapter::convert_create_statement(const hsql::CreateStatement* create_stmt) {
  auto columns = convert_columns(create_stmt);
  auto constraints = convert_constraints(create_stmt, columns);
  auto foreign_keys_spec = extract_foreign_keys_spec(create_stmt);

  return {create_stmt->tableName, std::move(columns), constraints, foreign_keys_spec, {}};
}

std::vector<TablePtr> ParserAdapter::parse(const std::string& sql) {
  std::vector<TablePtr> tables;

  const std::string normalized_sql = normalize_bare_char_type(sql);
  hsql::SQLParserResult result;
  hsql::SQLParser::parse(normalized_sql, &result);

  auto statements = result.getStatements();

  for (const auto* stmt : statements) {
    if (stmt == nullptr || !stmt->isType(hsql::StatementType::kStmtCreate)) {
      continue;
    }

    const auto* create_stmt = static_cast<const hsql::CreateStatement*>(stmt);

    tables.push_back(std::make_unique<Table>(convert_create_statement(create_stmt)));
  }

  return tables;
}

void ParserAdapter::foreign_key_resolver(std::vector<TablePtr>& tables) {
  for (auto& table_ptr : tables) {
    std::vector<ForeignKey> foreign_keys;
    Table* table = table_ptr.get();
    for (auto& foreign_key_spec : table->get_foreign_key_specs()) {
      Table* referenced_table = find_table_by_name(foreign_key_spec.referenced_table, tables);
      std::vector<Column*> local_columns_ptrs;
      std::vector<Column*> referenced_columns_ptrs;
      local_columns_ptrs.reserve(foreign_key_spec.local_columns.size());
      referenced_columns_ptrs.reserve(foreign_key_spec.referenced_columns.size());

      for (const auto& local_column : foreign_key_spec.local_columns) {
        local_columns_ptrs.push_back(find_column_by_name(local_column, table->get_columns()));
      }

      for (const auto& referenced_column : foreign_key_spec.referenced_columns) {
        referenced_columns_ptrs.push_back(
            find_column_by_name(referenced_column, referenced_table->get_columns()));
      }
      if (referenced_columns_ptrs.empty()) {
        referenced_columns_ptrs = primary_key_columns(referenced_table);
      }
      foreign_keys.emplace_back(local_columns_ptrs, referenced_table, referenced_columns_ptrs);
    }
    table->set_foreign_keys(foreign_keys);
  }
}

}  // namespace schemaforge
