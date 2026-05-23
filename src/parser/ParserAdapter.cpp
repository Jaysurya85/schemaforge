#include "schemaforge/parser/ParserAdapter.h"

#include "SQLParser.h"
#include "schemaforge/schema/Column.h"

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

TableConstraint ParserAdapter::convert_table_constraint(
    const hsql::TableConstraint* table_constraint) {
  std::vector<std::string> column_names;

  if (table_constraint != nullptr && table_constraint->columnNames != nullptr) {
    column_names.assign(table_constraint->columnNames->begin(),
                        table_constraint->columnNames->end());
  }

  return {convert_constraint_type(table_constraint->type), column_names};
}

bool ParserAdapter::should_store_as_table_constraint(TableConstraint& table_contraint) {
  if (table_contraint.columnNames.empty()) {
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
    const hsql::CreateStatement* create_stmt) {
  std::vector<TableConstraint> table_constraints;

  if (create_stmt == nullptr || create_stmt->tableConstraints == nullptr) {
    return table_constraints;
  }

  for (const auto* constraint : *create_stmt->tableConstraints) {
    TableConstraint table_constraint = convert_table_constraint(constraint);
    if (!should_store_as_table_constraint(table_constraint)) {
      continue;
    }
    table_constraints.push_back(convert_table_constraint(constraint));
  }

  return table_constraints;
}

std::vector<TableConstraint> ParserAdapter::extract_column_constraints(
    const hsql::CreateStatement* create_stmt) {
  std::vector<TableConstraint> column_constraints;

  if (create_stmt == nullptr || create_stmt->columns == nullptr) {
    return column_constraints;
  }

  for (const auto* col : *create_stmt->columns) {
    if (col == nullptr || col->column_constraints == nullptr) {
      continue;
    }

    for (const auto& constraint : *col->column_constraints) {
      TableConstraint column_constraint(convert_constraint_type(constraint),
                                        std::vector<std::string>{col->name});

      if (!should_store_as_table_constraint(column_constraint)) {
        continue;
      }
      column_constraints.push_back(std::move(column_constraint));
    }
  }

  return column_constraints;
}

std::vector<TableConstraint> ParserAdapter::convert_constraints(
    const hsql::CreateStatement* create_stmt) {
  auto constraints = extract_table_constraints(create_stmt);
  auto column_constraints = extract_column_constraints(create_stmt);

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

ForeignKey ParserAdapter::convert_foreign_key(
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

std::vector<ForeignKey> ParserAdapter::extract_table_foreign_keys(
    const hsql::CreateStatement* create_stmt) {
  std::vector<ForeignKey> foreign_keys;

  if (create_stmt == nullptr || create_stmt->tableConstraints == nullptr) {
    return foreign_keys;
  }

  for (const auto* constraint : *create_stmt->tableConstraints) {
    if (constraint == nullptr || constraint->type != hsql::ConstraintType::ForeignKey) {
      continue;
    }

    const auto* foreign_key_constraint = static_cast<const hsql::ForeignKeyConstraint*>(constraint);

    foreign_keys.push_back(convert_foreign_key(foreign_key_constraint));
  }
  return foreign_keys;
}

std::vector<ForeignKey> ParserAdapter::extract_column_foreign_keys(
    const hsql::CreateStatement* create_stmt) {
  std::vector<ForeignKey> foreign_keys;

  if (create_stmt == nullptr || create_stmt->columns == nullptr) {
    return foreign_keys;
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

      foreign_keys.emplace_back(local_columns, referenced_table, referenced_columns);
    }
  }
  return foreign_keys;
}

std::vector<ForeignKey> ParserAdapter::extract_foreign_keys(
    const hsql::CreateStatement* create_stmt) {
  auto foreign_keys = extract_table_foreign_keys(create_stmt);
  auto column_foreign_keys = extract_column_foreign_keys(create_stmt);

  foreign_keys.insert(foreign_keys.end(), std::make_move_iterator(column_foreign_keys.begin()),
                      std::make_move_iterator(column_foreign_keys.end()));

  return foreign_keys;
}

Column ParserAdapter::convert_column(const hsql::ColumnDefinition* col) {
  ColumnType column_type{convert_column_type(col->type)};
  return {col->name, column_type, col->nullable};
}

std::vector<Column> ParserAdapter::convert_columns(const hsql::CreateStatement* create_stmt) {
  std::vector<Column> columns;

  if (create_stmt == nullptr || create_stmt->columns == nullptr) {
    return columns;
  }

  for (const auto* col : *create_stmt->columns) {
    if (col == nullptr) {
      continue;
    }

    columns.push_back(convert_column(col));
  }

  return columns;
}

Table ParserAdapter::convert_create_statement(const hsql::CreateStatement* create_stmt) {
  auto constraints = convert_constraints(create_stmt);
  auto foreign_keys = extract_foreign_keys(create_stmt);
  auto columns = convert_columns(create_stmt);

  return {create_stmt->tableName, columns, constraints, foreign_keys};
}

std::vector<Table> ParserAdapter::parse(const std::string& sql) {
  std::vector<Table> tables;

  hsql::SQLParserResult result;
  hsql::SQLParser::parse(sql, &result);

  auto statements = result.getStatements();

  for (const auto* stmt : statements) {
    if (stmt == nullptr || !stmt->isType(hsql::StatementType::kStmtCreate)) {
      continue;
    }

    const auto* create_stmt = static_cast<const hsql::CreateStatement*>(stmt);

    tables.push_back(convert_create_statement(create_stmt));
  }

  return tables;
}

}  // namespace schemaforge
