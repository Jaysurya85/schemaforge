#include "schemaforge/generator/GenerationPlan.h"

#include <stdexcept>

#include "schemaforge/generator/ValueGenerator.h"

namespace schemaforge {

std::vector<ColumnData> GenerationPlan::generate_columns_data(const Table& table, int num_rows,
                                                              const GenerationConfig& config,
                                                              RandomEngine& random_engine,
                                                              const KeyRegistry& key_registry) {
  std::vector<ColumnData> column_data;
  const auto& columns = table.get_columns();
  column_data.reserve(columns.size());
  for (const auto& column_ptr : columns) {
    const Column* column = column_ptr.get();
    column_data.push_back(ColumnData{
        .column = column,
        .data = std::move(ValueGenerator::generate_column_data(
            *column, table, num_rows, config, random_engine, key_registry))});
  }
  return column_data;
}

std::vector<TableData> GenerationPlan::generate_table_data(const std::vector<TablePtr>& tables,
                                                           int num_rows) {
  GenerationConfig config;
  config.default_num_rows = num_rows;
  for (const auto& table_ptr : tables) {
    config.table_row_counts[table_ptr->get_table_name()] = num_rows;
  }

  return generate_table_data(tables, config);
}

std::vector<TableData> GenerationPlan::generate_table_data(const std::vector<TablePtr>& tables,
                                                           const GenerationConfig& config) {
  std::vector<TableData> table_data;
  table_data.reserve(tables.size());
  RandomEngine random_engine(config.seed);
  const KeyRegistry key_registry = KeyRegistry::build_from_tables(tables, config);
  for (const auto& table_ptr : tables) {
    const Table* table = table_ptr.get();
    const int num_rows = config.get_row_count(table->get_table_name());
    if (num_rows < 0) {
      throw std::runtime_error("Row count cannot be negative for table '" + table->get_table_name() +
                               "'");
    }

    table_data.push_back(TableData{
        .table = table,
        .columns = std::move(generate_columns_data(*table, num_rows, config, random_engine,
                                                   key_registry))});
  }
  return table_data;
}

std::vector<TableData> GenerationPlan::generate_table_data(
    const std::vector<TablePtr>& tables, const std::unordered_map<std::string, int>& row_counts,
    int default_num_rows) {
  GenerationConfig config;
  config.default_num_rows = default_num_rows;
  config.table_row_counts = row_counts;
  return generate_table_data(tables, config);
}

std::ostream& operator<<(std::ostream& os, const ColumnData& column_data) {
  os << "ColumnData(column: " << *column_data.column << ", data: [";
  for (const auto& value : column_data.data) {
    os << value << ", ";
  }
  os << "])";
  return os;
}

std::ostream& operator<<(std::ostream& os, const TableData& table_data) {
  os << table_data.table->get_table_name() << '\n';

  for (std::size_t column_index = 0; column_index < table_data.columns.size(); ++column_index) {
    if (column_index > 0) {
      os << " | ";
    }
    os << table_data.columns[column_index].column->get_column_name();
  }
  os << '\n';

  std::size_t row_count = 0;
  if (!table_data.columns.empty()) {
    row_count = table_data.columns.front().data.size();
  }

  for (std::size_t row_index = 0; row_index < row_count; ++row_index) {
    for (std::size_t column_index = 0; column_index < table_data.columns.size(); ++column_index) {
      if (column_index > 0) {
        os << " | ";
      }

      const auto& column_data = table_data.columns[column_index].data;
      if (row_index < column_data.size()) {
        os << column_data[row_index];
      }
    }

    if (row_index + 1 < row_count) {
      os << '\n';
    }
  }

  return os;
}

}  // namespace schemaforge
