#include "schemaforge/domain/ColumnDomainResolver.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace schemaforge {

namespace {

bool is_stricter_min(double current_value, bool current_inclusive, double candidate_value,
                     bool candidate_inclusive) {
  return candidate_value > current_value ||
         (candidate_value == current_value && current_inclusive && !candidate_inclusive);
}

bool is_stricter_max(double current_value, bool current_inclusive, double candidate_value,
                     bool candidate_inclusive) {
  return candidate_value < current_value ||
         (candidate_value == current_value && current_inclusive && !candidate_inclusive);
}

int integer_min_bound(const EffectiveCheckConstraint& check, double default_value) {
  if (!check.min_value.has_value()) {
    return static_cast<int>(std::ceil(default_value));
  }

  const double value = check.min_value.value();
  if (check.min_inclusive) {
    return static_cast<int>(std::ceil(value));
  }
  return static_cast<int>(std::floor(value)) + 1;
}

int integer_max_bound(const EffectiveCheckConstraint& check, double default_value) {
  if (!check.max_value.has_value()) {
    return static_cast<int>(std::floor(default_value));
  }

  const double value = check.max_value.value();
  if (check.max_inclusive) {
    return static_cast<int>(std::floor(value));
  }
  return static_cast<int>(std::ceil(value)) - 1;
}

}  // namespace

bool ColumnDomainResolver::is_integer_type(DataType data_type) {
  return data_type == DataType::INT || data_type == DataType::BIGINT ||
         data_type == DataType::SMALLINT;
}

bool ColumnDomainResolver::is_text_type(DataType data_type) {
  return data_type == DataType::TEXT || data_type == DataType::VARCHAR;
}

bool ColumnDomainResolver::is_decimal_type(DataType data_type) {
  return data_type == DataType::DECIMAL || data_type == DataType::FLOAT ||
         data_type == DataType::DOUBLE || data_type == DataType::REAL;
}

int ColumnDomainResolver::char_length(const Column* column) {
  const int64_t parsed_length = column->get_column_type().length;
  if (parsed_length <= 0) {
    return 1;
  }
  return static_cast<int>(parsed_length);
}

int ColumnDomainResolver::char_capacity(int length) {
  int capacity = 1;
  for (int index = 0; index < length; ++index) {
    if (capacity > std::numeric_limits<int>::max() / 26) {
      return std::numeric_limits<int>::max();
    }
    capacity *= 26;
  }
  return capacity;
}

GeneratedValue ColumnDomainResolver::coerce_allowed_value(const GeneratedValue& value,
                                                          DataType data_type) {
  return value.visit([data_type, &value](const auto& typed_value) -> GeneratedValue {
    using ValueType = std::decay_t<decltype(typed_value)>;
    if constexpr (std::is_same_v<ValueType, double>) {
      if (is_integer_type(data_type)) {
        return GeneratedValue::integer(static_cast<std::int64_t>(typed_value));
      }
      return GeneratedValue::numeric(typed_value);
    } else if constexpr (std::is_same_v<ValueType, std::string>) {
      return GeneratedValue::text(typed_value);
    } else {
      return value;
    }
  });
}

EffectiveCheckConstraint ColumnDomainResolver::effective_check_for_column(const Table* table,
                                                                          const Column* column) {
  EffectiveCheckConstraint effective;
  for (const auto& check : table->get_check_constraints()) {
    if (check.column != column) {
      continue;
    }

    if (check.type == CheckConstraintType::AllowedValues) {
      effective.allowed_values = check.allowed_values;
      continue;
    }

    if (check.type == CheckConstraintType::Range) {
      if (check.min_value.has_value()) {
        if (!effective.min_value.has_value() ||
            is_stricter_min(effective.min_value.value(), effective.min_inclusive,
                            check.min_value.value(), check.min_inclusive)) {
          effective.min_value = check.min_value;
          effective.min_inclusive = check.min_inclusive;
        }
      }
      if (check.max_value.has_value()) {
        if (!effective.max_value.has_value() ||
            is_stricter_max(effective.max_value.value(), effective.max_inclusive,
                            check.max_value.value(), check.max_inclusive)) {
          effective.max_value = check.max_value;
          effective.max_inclusive = check.max_inclusive;
        }
      }
    }
  }
  return effective;
}

EffectiveCheckConstraint ColumnDomainResolver::effective_check_for_column(const Table& table,
                                                                          const Column& column) {
  return effective_check_for_column(&table, &column);
}

ColumnDomain ColumnDomainResolver::domain_for_column(const Table* table, const Column* column,
                                                     const GenerationConfig& config,
                                                     int requested_rows) {
  (void)config;
  (void)requested_rows;

  ColumnDomain domain{.check = effective_check_for_column(table, column),
                      .finite_capacity = std::nullopt};
  const DataType data_type = column->get_column_type().data_type;

  if (!domain.check.allowed_values.empty()) {
    domain.finite_capacity = static_cast<int>(domain.check.allowed_values.size());
    return domain;
  }

  if (is_integer_type(data_type) && domain.check.min_value.has_value() &&
      domain.check.max_value.has_value()) {
    const int min_value = integer_min_bound(domain.check, domain.check.min_value.value());
    const int max_value = integer_max_bound(domain.check, domain.check.max_value.value());
    domain.finite_capacity = std::max(0, max_value - min_value + 1);
    return domain;
  }

  if (data_type == DataType::BOOLEAN) {
    domain.finite_capacity = 2;
    return domain;
  }

  if (data_type == DataType::CHAR) {
    domain.finite_capacity = char_capacity(char_length(column));
    return domain;
  }

  return domain;
}

}  // namespace schemaforge
