#include "schemaforge/schema/Table.h"

namespace schemaforge {
Table::Table(const std::string &table_name, const std::vector<Column> &columns,
             const std::vector<TableConstraint *> &table_contraints)
    : table_name(table_name), columns(columns),
      table_contraints(table_contraints) {}
std::string Table::get_table_name() const { return table_name; };
std::vector<Column> Table::get_columns() const { return columns; };
std::vector<TableConstraint *> Table::get_table_constraints() const {
  return table_contraints;
};
} // namespace schemaforge
