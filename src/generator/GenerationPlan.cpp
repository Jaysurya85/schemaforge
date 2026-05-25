#include "schemaforge/generator/GenerationPlan.h"

#include <algorithm>
#include <stdexcept>

#include "schemaforge/graph/DependencyGraph.h"

namespace schemaforge {

namespace {

bool contains_column(const std::vector<std::string>& column_names, const std::string& column_name) {
  return std::ranges::find(column_names, column_name) != column_names.end();
}

bool has_constraint(const Table& table, const Column& column, ConstraintType constraint_type) {
  const auto column_name = column.get_column_name();
  for (const auto& constraint : table.get_table_constraints()) {
    if (constraint.type == constraint_type &&
        contains_column(constraint.columnNames, column_name)) {
      return true;
    }
  }
  return false;
}

bool is_foreign_key(const Table& table, const Column& column) {
  const auto column_name = column.get_column_name();
  for (const auto& foreign_key : table.get_foreign_keys()) {
    if (contains_column(foreign_key.local_columns, column_name)) {
      return true;
    }
  }
  return false;
}

bool is_integer_type(DataType data_type) {
  return data_type == DataType::INT || data_type == DataType::BIGINT;
}

bool is_text_type(DataType data_type) {
  return data_type == DataType::TEXT || data_type == DataType::VARCHAR;
}

bool is_decimal_type(DataType data_type) {
  return data_type == DataType::DECIMAL || data_type == DataType::FLOAT ||
         data_type == DataType::DOUBLE || data_type == DataType::REAL;
}

}  // namespace

std::vector<Data> GenerationPlan::generate_column_data(const Column& column, const Table& table,
                                                       int num_rows) {
  const auto column_data_type = column.get_column_type().data_type;

  if (has_constraint(table, column, ConstraintType::PrimaryKey)) {
    if (!is_integer_type(column_data_type)) {
      throw std::runtime_error("Primary key column '" + column.get_column_name() +
                               "' must use an integer type for v0.1 generation");
    }
    IntGenerator generator(1);
    return generator.generate(num_rows);
  }

  if (is_foreign_key(table, column)) {
    if (!is_integer_type(column_data_type)) {
      throw std::runtime_error("Foreign key column '" + column.get_column_name() +
                               "' must use an integer type for v0.1 generation");
    }
    IntGenerator generator(1);
    return generator.generate(num_rows);
  }

  if (is_integer_type(column_data_type)) {
    IntGenerator generator(1);
    return generator.generate(num_rows);
  }

  if (is_text_type(column_data_type)) {
    TextGenerator generator(column.get_column_name());
    return generator.generate(num_rows);
  }

  if (is_decimal_type(column_data_type)) {
    DecimalGenerator generator(column.get_column_name());
    return generator.generate(num_rows);
  }

  if (column_data_type == DataType::BOOLEAN) {
    BooleanGenerator generator;
    return generator.generate(num_rows);
  }

  throw std::runtime_error("Unsupported data type for column '" + column.get_column_name() + "'");
}

std::vector<ColumnData> GenerationPlan::generate_columns_data(const Table& table, int num_rows) {
  std::vector<ColumnData> column_data;
  const auto columns = table.get_columns();
  column_data.reserve(columns.size());
  for (const auto& column : columns) {
    column_data.push_back(ColumnData{
        .column = column, .data = std::move(generate_column_data(column, table, num_rows))});
  }
  return column_data;
}

std::vector<TableData> GenerationPlan::generate_table_data(const std::vector<Table>& tables,
                                                           int num_rows) {
  TopologicalTableSortResult sort_result = DependencyGraph::sort_tables(tables);
  if (sort_result.has_cycle) {
    throw std::runtime_error(
        "Cannot generate table data because the schema has a dependency cycle");
  }

  std::vector<TableData> table_data;
  table_data.reserve(sort_result.tables.size());
  for (const auto& table : sort_result.tables) {
    table_data.push_back(TableData{.table_name = table.get_table_name(),
                                   .columns = std::move(generate_columns_data(table, num_rows))});
  }
  return table_data;
}

std::ostream& operator<<(std::ostream& os, const ColumnData& column_data) {
  os << "ColumnData(column: " << column_data.column << ", data: [";
  for (const auto& value : column_data.data) {
    os << value << ", ";
  }
  os << "])";
  return os;
}

std::ostream& operator<<(std::ostream& os, const TableData& table_data) {
  os << table_data.table_name << '\n';

  for (std::size_t column_index = 0; column_index < table_data.columns.size(); ++column_index) {
    if (column_index > 0) {
      os << " | ";
    }
    os << table_data.columns[column_index].column.get_column_name();
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
