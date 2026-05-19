#include "schemaforge/schema/Table.h"

namespace schemaforge {
Table::Table(const std::string &table_name, const std::vector<Column> &columns,
             const std::vector<TableConstraint> &table_contraints)
    : table_name(table_name), columns(columns),
      table_contraints(table_contraints) {}
std::string Table::get_table_name() const { return table_name; };
std::vector<Column> Table::get_columns() const { return columns; };
std::vector<TableConstraint> Table::get_table_constraints() const {
  return table_contraints;
}

std::ostream &operator<<(std::ostream &os, const TableConstraint &constraint) {
  os << "Table Contraint type: " << constraint.type << ", columns: [";
  for (const auto &column : constraint.columnNames) {
    os << column << ", ";
  }
  os << "])";
  return os;
}

std::ostream &operator<<(std::ostream &os, const Table &table) {
  os << "Table(name: " << table.get_table_name() << ", columns: [";
  for (const auto &column : table.get_columns()) {
    os << column << ", ";
  }
  os << "], constraints: [";
  for (const auto &constraint : table.get_table_constraints()) {
    os << constraint << ", ";
  }
  os << "])";
  return os;
}

std::ostream &operator<<(std::ostream &os,
                         const ConstraintType &constraint_type) {
  switch (constraint_type) {
  case ConstraintType::PrimaryKey:
    os << "PRIMARY KEY";
    break;
  case ConstraintType::ForeignKey:
    os << "FOREIGN KEY";
    break;
  case ConstraintType::NotNull:
    os << "NOT NULL";
    break;
  case ConstraintType::Null:
    os << "NULL";
    break;
  case ConstraintType::Unique:
    os << "UNIQUE";
    break;
  default:
    os << "UNKNOWN CONSTRAINT";
  }
  return os;
}

} // namespace schemaforge
