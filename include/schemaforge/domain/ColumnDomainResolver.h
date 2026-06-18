#pragma once

#include <optional>
#include <vector>

#include "schemaforge/config/GenerationConfig.h"
#include "schemaforge/generator/GeneratedValue.h"
#include "schemaforge/schema/Column.h"
#include "schemaforge/schema/Table.h"

namespace schemaforge {

struct EffectiveCheckConstraint {
  std::optional<double> min_value;
  std::optional<double> max_value;
  bool min_inclusive{true};
  bool max_inclusive{true};
  std::vector<GeneratedValue> allowed_values;
};

struct ColumnDomain {
  EffectiveCheckConstraint check;
  std::optional<int> finite_capacity;
};

class ColumnDomainResolver {
 public:
  ColumnDomainResolver() = default;

  static bool is_integer_type(DataType data_type);
  static bool is_text_type(DataType data_type);
  static bool is_decimal_type(DataType data_type);

  static int char_length(const Column* column);
  static int char_capacity(int length);

  static GeneratedValue coerce_allowed_value(const GeneratedValue& value, DataType data_type);

  static EffectiveCheckConstraint effective_check_for_column(const Table* table,
                                                             const Column* column);
  static EffectiveCheckConstraint effective_check_for_column(const Table& table,
                                                             const Column& column);

  static ColumnDomain domain_for_column(const Table* table, const Column* column,
                                        const GenerationConfig& config, int requested_rows);
};

}  // namespace schemaforge
