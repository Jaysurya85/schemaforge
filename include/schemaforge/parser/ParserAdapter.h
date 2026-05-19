#pragma once

#include "../schema/Table.h"
#include "sql/ColumnType.h"
#include "sql/CreateStatement.h"

#include <string>
#include <vector>

namespace schemaforge {

class ParserAdapter {
private:
  DataType convert_data_type(hsql::DataType data_type);

  ColumnType convert_column_type(const hsql::ColumnType &hsql_column_type);

  ConstraintType
  convert_constraint_type(const hsql::ConstraintType &constraint_type);

  TableConstraint
  convert_table_constraint(const hsql::TableConstraint *table_constraint);

  std::vector<TableConstraint>
  extract_table_constraints(const hsql::CreateStatement *create_stmt);

  bool should_store_as_table_constraint(TableConstraint table_contraint) const;

  std::vector<TableConstraint>
  extract_column_constraints(const hsql::CreateStatement *create_stmt);

  std::vector<TableConstraint>
  convert_constraints(const hsql::CreateStatement *create_stmt);

  Column convert_column(const hsql::ColumnDefinition *col);

  std::vector<Column> convert_columns(const hsql::CreateStatement *create_stmt);

  Table convert_create_statement(const hsql::CreateStatement *create_stmt);

public:
  std::vector<Table> parse(const std::string &sql);
};

} // namespace schemaforge
