#pragma once
#include "Column.h"
#include <string>
#include <vector>

namespace schemaforge {

enum struct ConstraintType { ForeignKey, NotNull, Null, PrimaryKey, Unique };

struct TableConstraint {
  TableConstraint(ConstraintType keyType,
                  std::vector<std::string> *columnNames);

  ConstraintType type;
  std::vector<std::string> *columnNames;
};

class Table {
private:
  std::string table_name;
  std::vector<Column> columns;

  std::vector<TableConstraint *> table_contraints;

public:
  Table(const std::string &table_name, const std::vector<Column> &columns,
        const std::vector<TableConstraint *> &table_contraints);
  std::string get_table_name() const;
  std::vector<Column> get_columns() const;
  std::vector<TableConstraint *> get_table_constraints() const;
};
} // namespace schemaforge
