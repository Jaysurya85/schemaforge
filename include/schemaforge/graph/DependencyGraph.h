#pragma once
#include "schemaforge/schema/Table.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace schemaforge {
using TableId = std::string;

struct TopologicalSortResult {
  bool has_cycle{false};
  std::vector<TableId> order{};
};

class DependencyGraph {
private:
  std::unordered_map<TableId, std::vector<TableId>> graph;

  std::unordered_map<TableId, int> in_degree;
  void add_table(const TableId &table_id);
  void add_dependency(const TableId &from, const TableId &to);

public:
  DependencyGraph();
  void make_graph(const std::vector<Table> &tables);
  auto &get_graph() const;
  TopologicalSortResult topological_sort() const;
};

std::ostream &operator<<(std::ostream &os, const DependencyGraph &graph);
} // namespace schemaforge
