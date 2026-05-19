#pragma once
#include "Column.h"
#include <string>
#include <vector>

namespace schemaforge {

enum struct ConstraintType {
  ForeignKey,
  NotNull,
  Null,
  PrimaryKey,
  Unique,
  Unknown
};

struct TableConstraint {

  ConstraintType type{ConstraintType::NotNull};
  std::vector<std::string> columnNames{nullptr};
};

struct ForeignKey {
  std::vector<std::string> local_columns{nullptr};
  std::string referenced_table{""};
  std::vector<std::string> referenced_columns{nullptr};
};

class Table {
private:
  std::string table_name;
  std::vector<Column> columns;

  std::vector<TableConstraint> table_contraints;
  std::vector<ForeignKey> foreign_keys;

public:
  Table(const std::string &table_name, const std::vector<Column> &columns,
        const std::vector<TableConstraint> &table_contraints,
        const std::vector<ForeignKey> &foreign_keys);
  std::string get_table_name() const;
  std::vector<Column> get_columns() const;
  std::vector<TableConstraint> get_table_constraints() const;
  std::vector<ForeignKey> get_foreign_keys() const;
};

std::ostream &operator<<(std::ostream &os, const TableConstraint &constraint);
std::ostream &operator<<(std::ostream &os, const Table &table);
std::ostream &operator<<(std::ostream &os,
                         const ConstraintType &constraint_type);
std::ostream &operator<<(std::ostream &os, const ForeignKey &foreign_key);
} // namespace schemaforge
