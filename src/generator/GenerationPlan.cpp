#include "schemaforge/generator/GenerationPlan.h"

namespace schemaforge {

std::vector<Data> GenerationPlan::generate_column_data(const Column& columns, int num_rows) {
  const auto& column_data_type = columns.get_column_type().data_type;
  switch (column_data_type) {
    case DataType::INT: {
      IntGenerator generator;
      auto int_data = generator.generate(num_rows);
      std::vector<Data> data;
      data.reserve(num_rows);
      for (const auto& value : int_data) {
        std::string val = std::to_string(std::stoi(value) + generator.min);
        data.push_back(std::move(val));
      }
      return data;
    }
    default:
      throw std::runtime_error("Unsupported data type");
  }
}

std::vector<ColumnData> GenerationPlan::generate_columns_data(const std::vector<Column>& columns,
                                                              int num_rows) {
  std::vector<ColumnData> column_data;
  column_data.reserve(columns.size());
  for (const auto& column : columns) {
    column_data.push_back(
        ColumnData{.column = column, .data = std::move(generate_column_data({column}, num_rows))});
  }
  return column_data;
}

std::vector<TableData> GenerationPlan::generate_table_data(const std::vector<Table>& tables,
                                                           int num_rows) {
  std::vector<TableData> table_data;
  table_data.reserve(tables.size());
  for (const auto& table : tables) {
    table_data.push_back(
        TableData{.table_name = table.get_table_name(),
                  .columns = std::move(generate_columns_data(table.get_columns(), num_rows))});
  }
  return table_data;
}

}  // namespace schemaforge
