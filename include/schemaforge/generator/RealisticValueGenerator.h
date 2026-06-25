#pragma once

#include <cstddef>
#include <optional>

#include "schemaforge/config/GenerationConfig.h"
#include "schemaforge/generator/GeneratedValue.h"
#include "schemaforge/schema/Column.h"
#include "schemaforge/schema/Table.h"

namespace schemaforge {

class RealisticValueGenerator {
 public:
  static std::optional<GeneratedValue> generate(const Table& table, const Column& column,
                                                std::size_t row_index,
                                                const GenerationConfig& config,
                                                bool require_unique = false);

  static std::optional<GeneratedValue> generate_heuristic(const Table& table, const Column& column,
                                                          std::size_t row_index,
                                                          const GenerationConfig& config,
                                                          bool require_unique = false);

  static double deterministic_fraction(unsigned int seed, const std::string& table_name,
                                       const std::string& discriminator,
                                       std::size_t row_index);
};

}  // namespace schemaforge
