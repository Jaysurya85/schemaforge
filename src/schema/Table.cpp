#include "schemaforge/schema/Table.h"

namespace schemaforge {
Table::Table(const std::string &table_name, const std::vector<Column> &columns,
             const std::vector<TableConstraint> &table_contraints,
             const std::vector<ForeignKey> &foreign_keys)
    : table_name(table_name), columns(columns),
      table_contraints(table_contraints), foreign_keys(foreign_keys) {}
std::string Table::get_table_name() const { return table_name; };
std::vector<Column> Table::get_columns() const { return columns; };
std::vector<TableConstraint> Table::get_table_constraints() const {
  return table_contraints;
}
std::vector<ForeignKey> Table::get_foreign_keys() const {
  return foreign_keys;
};

std::ostream &operator<<(std::ostream &os, const TableConstraint &constraint) {
  os << "Table Contraint type: " << constraint.type << ", columns: [";
  for (const auto &column : constraint.columnNames) {
    os << column << ", ";
  }
  os << "])";
  return os;
}

std::ostream &operator<<(std::ostream &os, const ForeignKey &foreign_key) {
  os << "Foreign Key(local columns: [";
  for (const auto &local_column : foreign_key.local_columns) {
    os << local_column << ", ";
  }
  os << "], referenced table: " << foreign_key.referenced_table
     << ", referenced columns: [";
  for (const auto &referenced_column : foreign_key.referenced_columns) {
    os << referenced_column << ", ";
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
  for (const auto &foreign_key : table.get_foreign_keys()) {
    os << foreign_key << ", ";
  }
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
