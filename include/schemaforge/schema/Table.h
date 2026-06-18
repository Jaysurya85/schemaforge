#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Column.h"
#include "schemaforge/generator/GeneratedValue.h"

namespace schemaforge {

class Table;
using ColumnPtr = std::unique_ptr<Column>;
using TablePtr = std::unique_ptr<Table>;
enum struct ConstraintType : std::uint8_t {
  ForeignKey,
  NotNull,
  Null,
  PrimaryKey,
  Unique,
  Unknown
};

struct TableConstraint {
  TableConstraint(const ConstraintType& type, const std::vector<Column*>& columns)
      : type(type), columns(columns) {};
  ConstraintType type;
  std::vector<Column*> columns;
};

struct ForeignKeySpec {
  ForeignKeySpec(std::vector<std::string> local_columns, std::string referenced_table,
                 std::vector<std::string> referenced_columns)
      : local_columns(std::move(local_columns)),
        referenced_table(std::move(referenced_table)),
        referenced_columns(std::move(referenced_columns)) {};
  std::vector<std::string> local_columns;
  std::string referenced_table;
  std::vector<std::string> referenced_columns;
};

struct ForeignKey {
  ForeignKey(std::vector<Column*>& local_columns, Table* referenced_table,
             std::vector<Column*>& referenced_columns)
      : local_columns(std::move(local_columns)),
        referenced_table(referenced_table),
        referenced_columns(std::move(referenced_columns)) {};
  std::vector<Column*> local_columns;
  Table* referenced_table;
  std::vector<Column*> referenced_columns;
};

enum class CheckConstraintType : std::uint8_t {
  Range,
  AllowedValues,
  Unsupported
};

struct ColumnCheckConstraint {
  CheckConstraintType type{CheckConstraintType::Unsupported};
  Column* column{nullptr};
  std::string column_name;
  std::optional<double> min_value;
  std::optional<double> max_value;
  bool min_inclusive{true};
  bool max_inclusive{true};
  std::vector<GeneratedValue> allowed_values;
  std::string expression;
  std::string raw_sql;
};

class Table {
 private:
  std::string table_name;
  std::vector<ColumnPtr> columns;
  std::vector<TableConstraint> table_contraints;
  std::vector<ForeignKeySpec> foreign_key_specs;
  std::vector<ForeignKey> foreign_keys;
  std::vector<ColumnCheckConstraint> check_constraints;

 public:
  Table(std::string table_name, std::vector<ColumnPtr> columns,
        std::vector<TableConstraint> table_contraints,
        std::vector<ForeignKeySpec> foreign_keys_spec, std::vector<ForeignKey> foreign_keys,
        std::vector<ColumnCheckConstraint> check_constraints = {});
  [[nodiscard]] std::string get_table_name() const;
  [[nodiscard]] const std::vector<ColumnPtr>& get_columns() const;
  [[nodiscard]] const std::vector<TableConstraint>& get_table_constraints() const;
  [[nodiscard]] const std::vector<ColumnCheckConstraint>& get_check_constraints() const;
  [[nodiscard]] const std::vector<ForeignKey>& get_foreign_keys() const;
  [[nodiscard]] const std::vector<ForeignKeySpec>& get_foreign_key_specs() const;
  void set_foreign_keys(const std::vector<ForeignKey>& foreign_keys);
};

std::ostream& operator<<(std::ostream& os, const TableConstraint& constraint);
std::ostream& operator<<(std::ostream& os, const Table& table);
std::ostream& operator<<(std::ostream& os, const ConstraintType& constraint_type);
std::ostream& operator<<(std::ostream& os, const ForeignKey& foreign_key);
}  // namespace schemaforge
