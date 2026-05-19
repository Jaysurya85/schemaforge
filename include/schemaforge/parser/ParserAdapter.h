#pragma once
#include "../schema/Table.h"
#include "sql/ColumnType.h"

namespace schemaforge {
class ParserAdapter {
private:
  schemaforge::DataType convert_data_type(hsql::DataType data_type);
  schemaforge::ColumnType
  convert_column_type(const hsql::ColumnType &hsql_column_type);

public:
  std::vector<Table> parse(const std::string &sql);
  std::string print(const std::vector<Table> &tables);
};
} // namespace schemaforge
