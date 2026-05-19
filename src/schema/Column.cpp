#include "schemaforge/schema/Column.h"

namespace schemaforge {

Column::Column(const std::string &column_name, ColumnType column_type)
    : column_name(column_name), column_type(column_type) {}
std::string Column::get_column_name() const { return column_name; };
ColumnType Column::get_column_type() const { return column_type; };
} // namespace schemaforge
