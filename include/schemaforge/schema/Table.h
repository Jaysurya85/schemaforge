#pragma once
#include "Column.h"
#include <string>
#include <vector>

namespace schemaforge {

enum struct ConstraintType { ForeignKey, NotNull, Null, PrimaryKey, Unique };

struct TableConstraint {

  ConstraintType type{ConstraintType::NotNull};
  std::vector<std::string> columnNames{nullptr};
};

class Table {
private:
  std::string table_name;
  std::vector<Column> columns;

  std::vector<TableConstraint> table_contraints;

public:
  Table(const std::string &table_name, const std::vector<Column> &columns,
        const std::vector<TableConstraint> &table_contraints);
  std::string get_table_name() const;
  std::vector<Column> get_columns() const;
  std::vector<TableConstraint> get_table_constraints() const;
};

std::ostream &operator<<(std::ostream &os, const TableConstraint &constraint);
std::ostream &operator<<(std::ostream &os, const Table &table);
std::ostream &operator<<(std::ostream &os,
                         const ConstraintType &constraint_type);
} // namespace schemaforge
