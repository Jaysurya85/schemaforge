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

void DependencyGraph::make_graph(const std::vector<Table>& tables) {
  graph.clear();
  in_degree.clear();
  table_order.clear();

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

TopologicalTableSortResult DependencyGraph::sort_tables(const std::vector<Table>& tables) {
  DependencyGraph dependency_graph;
  dependency_graph.make_graph(tables);
  TopologicalSortResult sort_result = dependency_graph.topological_sort();

  std::unordered_map<TableId, Table> table_by_name;
  table_by_name.reserve(tables.size());
  for (auto table : tables) {
    table_by_name.emplace(table.get_table_name(), std::move(table));
  }

  std::vector<Table> sorted_tables;
  sorted_tables.reserve(sort_result.order.size());
  for (const auto& table_name : sort_result.order) {
    const auto table = table_by_name.find(table_name);
    if (table == table_by_name.end()) {
      throw std::runtime_error("Table '" + table_name + "' was not found while sorting tables");
    }
    sorted_tables.push_back(std::move(table->second));
  }

  return {sort_result.has_cycle, std::move(sort_result.order), std::move(sorted_tables)};
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
