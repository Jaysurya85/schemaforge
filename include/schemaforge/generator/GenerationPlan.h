
#pragma once
#include <functional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "schemaforge/config/GenerationConfig.h"
#include "schemaforge/generator/GeneratedValue.h"
#include "schemaforge/generator/IntGenerator.h"
#include "schemaforge/generator/KeyRegistry.h"
#include "schemaforge/generator/RandomEngine.h"
#include "schemaforge/schema/Column.h"
#include "schemaforge/schema/Table.h"

namespace schemaforge {
struct ColumnData {
  const Column* column;
  std::vector<GeneratedValue> data;
};

struct TableData {
  const Table* table;
  std::vector<ColumnData> columns;
};

struct GeneratedRow {
  const Table* table;
  std::vector<const Column*> columns;
  std::vector<GeneratedValue> values;
};

struct GenerationStreamConsumer {
  std::function<void(const Table&)> table_started;
  std::function<void(const GeneratedRow&)> row_generated;
  std::function<void(const Table&)> table_finished;
};

class GenerationPlan {
 private:
  static std::vector<ColumnData> generate_columns_data(
      const Table& table, int num_rows, const GenerationConfig& config,
      RandomEngine& random_engine, const KeyRegistry& key_registry);

 public:
  static std::vector<TableData> generate_table_data(const std::vector<TablePtr>& tables,
                                                    int num_rows);
  static std::vector<TableData> generate_table_data(const std::vector<TablePtr>& tables,
                                                    const GenerationConfig& config);
  static std::vector<TableData> generate_table_data(
      const std::vector<TablePtr>& tables, const std::unordered_map<std::string, int>& row_counts,
      int default_num_rows);
  static void stream_table_data(const std::vector<TablePtr>& tables,
                                const GenerationConfig& config,
                                const std::function<void(const GeneratedRow&)>& row_consumer);
  static void stream_table_data(const std::vector<TablePtr>& tables,
                                const GenerationConfig& config,
                                const GenerationStreamConsumer& consumer);
};

std::ostream& operator<<(std::ostream& os, const ColumnData& column_data);
std::ostream& operator<<(std::ostream& os, const TableData& table_data);

}  // namespace schemaforge
