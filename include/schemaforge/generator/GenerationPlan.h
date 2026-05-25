
#pragma once
#include <ostream>
#include <string>
#include <vector>

#include "schemaforge/generator/BooleanGenerator.h"
#include "schemaforge/generator/DecimalGenerator.h"
#include "schemaforge/generator/IntGenerator.h"
#include "schemaforge/generator/TextGenerator.h"
#include "schemaforge/schema/Column.h"
#include "schemaforge/schema/Table.h"

namespace schemaforge {
struct ColumnData {
  Column column;
  std::vector<Data> data;
};

struct TableData {
  std::string table_name;
  std::vector<ColumnData> columns;
};

class GenerationPlan {
 private:
  static std::vector<Data> generate_column_data(const Column& column, const Table& table,
                                                int num_rows);
  static std::vector<ColumnData> generate_columns_data(const Table& table, int num_rows);

 public:
  static std::vector<TableData> generate_table_data(const std::vector<Table>& tables, int num_rows);
};

std::ostream& operator<<(std::ostream& os, const ColumnData& column_data);
std::ostream& operator<<(std::ostream& os, const TableData& table_data);

}  // namespace schemaforge
