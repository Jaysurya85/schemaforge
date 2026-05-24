#pragma once
#include <string>
#include <utility>
#include <vector>

#include "Column.h"

namespace schemaforge {

enum struct ConstraintType { ForeignKey, NotNull, Null, PrimaryKey, Unique, Unknown };

struct TableConstraint {
  TableConstraint(const ConstraintType& type, const std::vector<std::string>& columnNames)
      : type(type), columnNames(columnNames) {};
  ConstraintType type;
  std::vector<std::string> columnNames;
};

struct ForeignKey {
  ForeignKey(std::vector<std::string>& local_columns, std::string referenced_table,
             std::vector<std::string>& referenced_columns)
      : local_columns(std::move(local_columns)),
        referenced_table(std::move(referenced_table)),
        referenced_columns(std::move(referenced_columns)) {};
  std::vector<std::string> local_columns;
  std::string referenced_table;
  std::vector<std::string> referenced_columns;
};

class Table {
 private:
  std::string table_name;
  std::vector<Column> columns;

  std::vector<TableConstraint> table_contraints;
  std::vector<ForeignKey> foreign_keys;

 public:
  Table(std::string table_name, std::vector<Column> columns,
        std::vector<TableConstraint> table_contraints, std::vector<ForeignKey> foreign_keys);
  [[nodiscard]] std::string get_table_name() const;
  [[nodiscard]] std::vector<Column> get_columns() const;
  [[nodiscard]] std::vector<TableConstraint> get_table_constraints() const;
  [[nodiscard]] std::vector<ForeignKey> get_foreign_keys() const;
};

std::ostream& operator<<(std::ostream& os, const TableConstraint& constraint);
std::ostream& operator<<(std::ostream& os, const Table& table);
std::ostream& operator<<(std::ostream& os, const ConstraintType& constraint_type);
std::ostream& operator<<(std::ostream& os, const ForeignKey& foreign_key);
}  // namespace schemaforge
