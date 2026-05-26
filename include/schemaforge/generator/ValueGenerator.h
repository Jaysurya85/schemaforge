#pragma once
#include <vector>

#include "schemaforge/generator/GenerationConfig.h"
#include "schemaforge/generator/IntGenerator.h"
#include "schemaforge/generator/KeyRegistry.h"
#include "schemaforge/generator/RandomEngine.h"
#include "schemaforge/schema/Column.h"
#include "schemaforge/schema/Table.h"

namespace schemaforge {

class ValueGenerator {
 public:
  ValueGenerator() = default;

  static std::vector<Data> generate_column_data(const Column& column, const Table& table,
                                                int num_rows, const GenerationConfig& config,
                                                RandomEngine& random_engine,
                                                const KeyRegistry& key_registry);
};

}  // namespace schemaforge
