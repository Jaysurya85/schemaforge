#include "schemaforge/schema/Column.h"

namespace schemaforge {

Column::Column(const std::string& column_name, ColumnType column_type, bool nullable)
    : column_name(column_name), column_type(column_type), nullable(nullable) {}
std::string Column::get_column_name() const { return column_name; };
ColumnType Column::get_column_type() const { return column_type; };
bool Column::is_nullable() const { return nullable; }
std::ostream& operator<<(std::ostream& os, const Column& column) {
  os << "Column(name: " << column.get_column_name() << ", type: "
     << column.get_column_type().data_type
     // << ", length: " << column.get_column_type().length
     // << ", precision: " << column.get_column_type().precision
     // << ", scale: " << column.get_column_type().scale
     << ", nullable: " << (column.is_nullable() ? "true" : "false") << ")";
  return os;
}
std::ostream& operator<<(std::ostream& os, const DataType& data_type) {
  switch (data_type) {
    case DataType::BIGINT:
      os << "BIGINT";
      break;
    case DataType::BOOLEAN:
      os << "BOOLEAN";
      break;
    case DataType::CHAR:
      os << "CHAR";
      break;
    case DataType::DATE:
      os << "DATE";
      break;
    case DataType::DATETIME:
      os << "DATETIME";
      break;
    case DataType::DECIMAL:
      os << "DECIMAL";
      break;
    case DataType::DOUBLE:
      os << "DOUBLE";
      break;
    case DataType::FLOAT:
      os << "FLOAT";
      break;
    case DataType::INT:
      os << "INT";
      break;
    case DataType::LONG:
      os << "LONG";
      break;
    case DataType::REAL:
      os << "REAL";
      break;
    case DataType::SMALLINT:
      os << "SMALLINT";
      break;
    case DataType::TEXT:
      os << "TEXT";
      break;
    case DataType::TIME:
      os << "TIME";
      break;
    case DataType::VARCHAR:
      os << "VARCHAR";
      break;
    default:
      os << "UNKNOWN";
  }
  return os;
}
}  // namespace schemaforge
