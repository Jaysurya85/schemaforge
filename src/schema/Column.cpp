#include "schemaforge/schema/Column.h"

namespace schemaforge {

Column::Column(const std::string &column_name, ColumnType column_type,
               bool nullable)
    : column_name(column_name), column_type(column_type), nullable(nullable) {}
std::string Column::get_column_name() const { return column_name; };
ColumnType Column::get_column_type() const { return column_type; };
bool Column::is_nullable() const { return nullable; }
std::ostream &operator<<(std::ostream &os, const Column &column) {
  os << "Column(name: " << column.get_column_name() << ", type: "
     << static_cast<int>(column.get_column_type().data_type)
     // << ", length: " << column.get_column_type().length
     // << ", precision: " << column.get_column_type().precision
     // << ", scale: " << column.get_column_type().scale
     << ", nullable: " << (column.is_nullable() ? "true" : "false") << ")";
  return os;
}
} // namespace schemaforge
