#pragma once
#include "../schema/Table.h"
#include "sql/ColumnType.h"
#include "sql/CreateStatement.h"

namespace schemaforge {
class ParserAdapter {
private:
  schemaforge::DataType convert_data_type(hsql::DataType data_type);
  schemaforge::ColumnType
  convert_column_type(const hsql::ColumnType &hsql_column_type);
  schemaforge::ConstraintType
  convert_constraint_type(const hsql::ConstraintType &constraint_type);
  schemaforge::TableConstraint
  convert_table_constraint(const hsql::TableConstraint *table_constraint);

public:
  std::vector<Table> parse(const std::string &sql);
};
} // namespace schemaforge
