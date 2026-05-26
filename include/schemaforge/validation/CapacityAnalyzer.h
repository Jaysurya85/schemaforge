#pragma once
#include <optional>
#include <string>
#include <vector>

#include "schemaforge/schema/Table.h"

namespace schemaforge {

struct RowCapacityLimit {
  int max_rows;
  std::string reason;
};

struct TableCapacityInfo {
  std::string table_name;
  std::optional<RowCapacityLimit> static_max_rows;
  std::vector<std::string> reasons;
};

struct SchemaCapacityInfo {
  std::vector<TableCapacityInfo> tables;

  [[nodiscard]] const TableCapacityInfo* find_table(const std::string& table_name) const;
};

class CapacityAnalyzer {
 private:
  static bool contains_column(const std::vector<std::string>& column_names,
                              const std::string& column_name);
  static bool has_constraint(const Table& table, const Column& column,
                             ConstraintType constraint_type);
  static void apply_capacity_limit(TableCapacityInfo& table_info,
                                   const RowCapacityLimit& capacity_limit);

 public:
  CapacityAnalyzer() = default;
  static SchemaCapacityInfo analyze(const std::vector<Table>& tables);
};

}  // namespace schemaforge
