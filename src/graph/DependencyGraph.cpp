#include "schemaforge/graph/DependencyGraph.h"

#include <algorithm>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace schemaforge {

DependencyGraph::DependencyGraph() : graph({}), in_degree({}), table_order({}) {}

void DependencyGraph::add_table(const TableId& table_id) {
  if (!graph.contains(table_id)) {
    graph[table_id] = std::vector<TableId>();
    table_order.push_back(table_id);
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

void DependencyGraph::make_graph(const std::vector<TablePtr>& tables) {
  graph.clear();
  in_degree.clear();
  table_order.clear();

  for (const auto& table_ptr : tables) {
    add_table(table_ptr->get_table_name());
  }

  for (const auto& table_ptr : tables) {
    for (const auto& foreign_key_spec : table_ptr->get_foreign_key_specs()) {
      add_dependency(foreign_key_spec.referenced_table, table_ptr->get_table_name());
    }
  }
}

auto& DependencyGraph::get_graph() const { return graph; }

TopologicalSortResult DependencyGraph::topological_sort() const {
  auto current_in_degree = in_degree;
  TopologicalSortResult result(false, {});
  std::queue<TableId> queue;

  for (const auto& table_id : table_order) {
    if (current_in_degree[table_id] == 0) {
      queue.push(table_id);
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

TopologicalTableSortResult DependencyGraph::topological_sort_tables(
    const std::vector<TablePtr>& tables) const {
  TopologicalSortResult sort_result = topological_sort();

  return {sort_result.has_cycle, std::move(sort_result.order)};
}

TopologicalTableSortResult DependencyGraph::sort_tables(const std::vector<TablePtr>& tables) {
  DependencyGraph dependency_graph;
  dependency_graph.make_graph(tables);
  return dependency_graph.topological_sort_tables(tables);
}

std::vector<TablePtr> DependencyGraph::get_sorted_tables(
    std::vector<TablePtr> tables, const std::vector<TableId>& sorted_table_ids) {
  std::unordered_map<TableId, std::size_t> table_map;
  for (int i = 0; i < tables.size(); i++) {
    table_map[tables[i]->get_table_name()] = i;
  }

  std::vector<TablePtr> sorted_tables;
  for (const auto& table_id : sorted_table_ids) {
    auto it = table_map.find(table_id);
    if (it != table_map.end()) {
      sorted_tables.push_back(std::move(tables[it->second]));
    } else {
      throw std::runtime_error("Table ID not found: " + table_id);
    }
  }

  return sorted_tables;
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
