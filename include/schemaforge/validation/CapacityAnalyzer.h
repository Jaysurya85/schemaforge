#pragma once
#include <optional>
#include <string>
#include <vector>

#include "schemaforge/generator/GenerationConfig.h"
#include "schemaforge/graph/DependencyGraph.h"
#include "schemaforge/schema/Table.h"

namespace schemaforge {

struct RowCapacityLimit {
  int max_rows;
  std::string reason;
};

struct TableCapacityInfo {
  Table* table;
  TableId table_id;
  int requested_rows;
  int max_rows;
  std::vector<std::string> reasons;
};

struct SchemaCapacityInfo {
  std::vector<TableCapacityInfo> tables;
};

class CapacityAnalyzer {
 private:
  static bool contains_column(const std::vector<Column*>& columns, const Column* column);
  static bool has_constraint(const Table* table, const Column* column,
                             ConstraintType constraint_type);
  static std::optional<RowCapacityLimit> column_capacity_limit(const Table* table,
                                                               const Column* column);
  static void apply_capacity_limit(TableCapacityInfo& table_info,
                                   const RowCapacityLimit& capacity_limit);

 public:
  CapacityAnalyzer() = default;
  static SchemaCapacityInfo analyze(const std::vector<TablePtr>& tables,
                                    const GenerationConfig& config);
};

}  // namespace schemaforge
