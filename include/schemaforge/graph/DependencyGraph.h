#pragma once
#include <string>
#include <unordered_map>
#include <vector>

#include "schemaforge/schema/Table.h"

namespace schemaforge {
using TableId = std::string;

struct TopologicalSortResult {
  TopologicalSortResult(bool has_cycle, std::vector<TableId> order)
      : has_cycle(has_cycle), order(std::move(order)) {}
  bool has_cycle;
  std::vector<TableId> order;
};

struct TopologicalTableSortResult {
  TopologicalTableSortResult(bool has_cycle, std::vector<TableId> order, std::vector<Table> tables)
      : has_cycle(has_cycle), order(std::move(order)), tables(std::move(tables)) {}
  bool has_cycle;
  std::vector<TableId> order;
  std::vector<Table> tables;
};

class DependencyGraph {
 private:
  std::unordered_map<TableId, std::vector<TableId>> graph;

  std::unordered_map<TableId, int> in_degree;
  std::vector<TableId> table_order;
  void add_table(const TableId& table_id);
  void add_dependency(const TableId& from, const TableId& to);

 public:
  DependencyGraph();
  void make_graph(const std::vector<Table>& tables);
  auto& get_graph() const;
  TopologicalSortResult topological_sort() const;
  TopologicalTableSortResult topological_sort_tables(const std::vector<Table>& tables) const;
  static TopologicalTableSortResult sort_tables(const std::vector<Table>& tables);
};

std::ostream& operator<<(std::ostream& os, const DependencyGraph& graph);
}  // namespace schemaforge
