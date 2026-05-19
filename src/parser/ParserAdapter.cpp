#include "schemaforge/parser/ParserAdapter.h"
#include "SQLParser.h"
#include "schemaforge/schema/Column.h"

namespace schemaforge {

schemaforge::DataType
ParserAdapter::convert_data_type(hsql::DataType data_type) {
  switch (data_type) {
  case hsql::DataType::BIGINT:
    return schemaforge::DataType::BIGINT;
  case hsql::DataType::BOOLEAN:
    return schemaforge::DataType::BOOLEAN;
  case hsql::DataType::CHAR:
    return schemaforge::DataType::CHAR;
  case hsql::DataType::DATE:
    return schemaforge::DataType::DATE;
  case hsql::DataType::DATETIME:
    return schemaforge::DataType::DATETIME;
  case hsql::DataType::DECIMAL:
    return schemaforge::DataType::DECIMAL;
  case hsql::DataType::DOUBLE:
    return schemaforge::DataType::DOUBLE;
  case hsql::DataType::FLOAT:
    return schemaforge::DataType::FLOAT;
  case hsql::DataType::INT:
    return schemaforge::DataType::INT;
  case hsql::DataType::LONG:
    return schemaforge::DataType::LONG;
  case hsql::DataType::REAL:
    return schemaforge::DataType::REAL;
  case hsql::DataType::SMALLINT:
    return schemaforge::DataType::SMALLINT;
  case hsql::DataType::TEXT:
    return schemaforge::DataType::TEXT;
  case hsql::DataType::TIME:
    return schemaforge::DataType::TIME;
  case hsql::DataType ::VARCHAR:
    return schemaforge ::DataType ::VARCHAR;
  default:
    return schemaforge ::DataType ::UNKNOWN;
  }
}

schemaforge::ColumnType
ParserAdapter::convert_column_type(const hsql::ColumnType &hsql_column_type) {
  return schemaforge::ColumnType{
      convert_data_type(hsql_column_type.data_type), hsql_column_type.length,
      hsql_column_type.precision, hsql_column_type.scale};
}

schemaforge::ConstraintType ParserAdapter::convert_constraint_type(
    const hsql::ConstraintType &constraint_type) {

  switch (constraint_type) {
  case hsql::ConstraintType::PrimaryKey:
    return schemaforge::ConstraintType::PrimaryKey;
  case hsql::ConstraintType::ForeignKey:
    return schemaforge::ConstraintType::ForeignKey;
  case hsql::ConstraintType::NotNull:
    return schemaforge::ConstraintType::NotNull;
  case hsql::ConstraintType::Null:
    return schemaforge::ConstraintType::Null;
  case hsql::ConstraintType::Unique:
    return schemaforge::ConstraintType::Unique;
  default:
    return schemaforge::ConstraintType::NotNull;
  }
}

schemaforge::TableConstraint ParserAdapter::convert_table_constraint(
    const hsql::TableConstraint *table_constraint) {
  std::vector<std::string> column_names(table_constraint->columnNames->begin(),
                                        table_constraint->columnNames->end());
  return schemaforge::TableConstraint(
      convert_constraint_type(table_constraint->type), std::move(column_names));
}

std::vector<Table> ParserAdapter::parse(const std::string &sql) {
  std::vector<Table> tables;

  hsql::SQLParserResult result;
  hsql::SQLParser::parse(sql, &result);
  std::vector<hsql::SQLStatement *> statements = result.getStatements();

  for (const auto &stmt : statements) {
    if (stmt->isType(hsql::StatementType::kStmtCreate)) {
      auto *create_stmt = static_cast<hsql::CreateStatement *>(stmt);

      std::vector<TableConstraint> table_contraints;
      for (const auto &contraints : *create_stmt->tableConstraints) {
        TableConstraint table_constraint = convert_table_constraint(contraints);
        table_contraints.push_back(std::move(table_constraint));
      }

      std::vector<Column> columns;
      for (const auto &col : *create_stmt->columns) {
        ColumnType column_type{convert_column_type(col->type)};
        columns.push_back(Column(col->name, column_type, col->nullable));
        for (const auto &constraint : *col->column_constraints) {
          TableConstraint column_constraint =
              TableConstraint(convert_constraint_type(constraint),
                              std::vector<std::string>{col->name});
          table_contraints.push_back(std::move(column_constraint));
        }
      }
      tables.push_back(
          Table(create_stmt->tableName, columns, table_contraints));
    }
  }

  return tables;
}

} // namespace schemaforge
