#include "schemaforge/schema/Table.h"

namespace schemaforge {
Table::Table(std::string table_name, std::vector<ColumnPtr> columns,
             std::vector<TableConstraint> table_contraints,
             std::vector<ForeignKeySpec> foreign_key_specs, std::vector<ForeignKey> foreign_keys,
             std::vector<ColumnCheckConstraint> check_constraints)
    : table_name(std::move(table_name)),
      columns(std::move(columns)),
      table_contraints(std::move(table_contraints)),
      foreign_key_specs(std::move(foreign_key_specs)),
      foreign_keys(std::move(foreign_keys)),
      check_constraints(std::move(check_constraints)) {}
std::string Table::get_table_name() const { return table_name; };
const std::vector<ColumnPtr>& Table::get_columns() const { return columns; };
const std::vector<TableConstraint>& Table::get_table_constraints() const { return table_contraints; }
const std::vector<ColumnCheckConstraint>& Table::get_check_constraints() const {
  return check_constraints;
}
const std::vector<ForeignKeySpec>& Table::get_foreign_key_specs() const { return foreign_key_specs; }
const std::vector<ForeignKey>& Table::get_foreign_keys() const { return foreign_keys; };

void Table::set_foreign_keys(const std::vector<ForeignKey>& fk) { foreign_keys = fk; }

std::ostream& operator<<(std::ostream& os, const TableConstraint& constraint) {
  os << "Table Contraint type: " << constraint.type << ", columns: [";
  for (const auto& column : constraint.columns) {
    os << column->get_column_name() << ", ";
  }
  os << "])";
  return os;
}

std::ostream& operator<<(std::ostream& os, const ForeignKey& foreign_key) {
  os << "Foreign Key(local columns: [";
  for (const auto& local_column : foreign_key.local_columns) {
    os << local_column->get_column_name() << ", ";
  }
  os << "], referenced table: " << foreign_key.referenced_table->get_table_name()
     << ", referenced columns: [";
  for (const auto& referenced_column : foreign_key.referenced_columns) {
    os << referenced_column->get_column_name() << ", ";
  }
  os << "])";
  return os;
}

std::ostream& operator<<(std::ostream& os, const Table& table) {
  os << "Table(name: " << table.get_table_name() << ", columns: [";
  for (const auto& column : table.get_columns()) {
    os << *(column.get()) << ", ";
  }
  os << "], constraints: [";
  for (const auto& constraint : table.get_table_constraints()) {
    os << constraint << ", ";
  }
  for (const auto& foreign_key : table.get_foreign_keys()) {
    os << foreign_key << ", ";
  }
  return os;
}

std::ostream& operator<<(std::ostream& os, const ConstraintType& constraint_type) {
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

}  // namespace schemaforge
