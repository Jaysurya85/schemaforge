#include "schemaforge/graph/DependencyGraph.h"

#include <algorithm>
#include <queue>

namespace schemaforge {

DependencyGraph::DependencyGraph() : graph({}), in_degree({}) {}

void DependencyGraph::add_table(const TableId& table_id) {
  if (!graph.contains(table_id)) {
    graph[table_id] = std::vector<TableId>();
  }
  if (!in_degree.contains(table_id)) {
    in_degree[table_id] = 0;
  }
}

void DependencyGraph::add_dependency(const TableId& from, const TableId& to) {
  auto& dependencies = graph[from];
  if (std::ranges::find(dependencies, to) != dependencies.end()) {
    return;
  }

  dependencies.push_back(to);
  in_degree[to]++;
}

void DependencyGraph::make_graph(const std::vector<Table>& tables) {
  graph.clear();
  in_degree.clear();

  for (const auto& table : tables) {
    add_table(table.get_table_name());
  }

  for (const auto& table : tables) {
    for (const auto& foreign_key : table.get_foreign_keys()) {
      add_dependency(foreign_key.referenced_table, table.get_table_name());
    }
  }
}

auto& DependencyGraph::get_graph() const { return graph; }

TopologicalSortResult DependencyGraph::topological_sort() const {
  auto current_in_degree = in_degree;
  TopologicalSortResult result(false, {});
  std::queue<TableId> queue;

  for (const auto& node : current_in_degree) {
    if (node.second == 0) {
      queue.push(node.first);
    }
  }

  while (!queue.empty()) {
    TableId node = queue.front();
    queue.pop();
    result.order.push_back(node);

    auto neighbors = graph.find(node);
    if (neighbors == graph.end()) {
      continue;
    }

    for (const auto& neighbor : neighbors->second) {
      current_in_degree[neighbor]--;
      if (current_in_degree[neighbor] == 0) {
        queue.push(neighbor);
      }
    }
  }

  result.has_cycle = result.order.size() != graph.size();

  return result;
}

std::ostream& operator<<(std::ostream& os, const DependencyGraph& dependency_graph) {
  os << "Dependency Graph: " << '\n';
  for (const auto& node : dependency_graph.get_graph()) {
    os << node.first << " -> [";
    for (const auto& dependency : node.second) {
      os << dependency << ", ";
    }
    os << "]" << '\n';
  }
  return os;
}
}  // namespace schemaforge
