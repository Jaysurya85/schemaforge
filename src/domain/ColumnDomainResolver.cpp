#include "schemaforge/domain/ColumnDomainResolver.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <cstdio>
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

std::optional<GeneratedValue> ColumnDomainResolver::coerce_configured_value(
    const GeneratedValue& value, DataType data_type) {
  return value.visit([data_type](const auto& typed_value) -> std::optional<GeneratedValue> {
    using ValueType = std::decay_t<decltype(typed_value)>;
    if constexpr (std::is_same_v<ValueType, std::int64_t>) {
      if (is_integer_type(data_type)) {
        return GeneratedValue::integer(typed_value);
      }
      if (is_decimal_type(data_type)) {
        return GeneratedValue::numeric(static_cast<double>(typed_value));
      }
    } else if constexpr (std::is_same_v<ValueType, double>) {
      if (is_decimal_type(data_type)) {
        return GeneratedValue::numeric(typed_value);
      }
      if (is_integer_type(data_type) && std::floor(typed_value) == typed_value) {
        return GeneratedValue::integer(static_cast<std::int64_t>(typed_value));
      }
    } else if constexpr (std::is_same_v<ValueType, bool>) {
      if (data_type == DataType::BOOLEAN) {
        return GeneratedValue::boolean(typed_value);
      }
    } else if constexpr (std::is_same_v<ValueType, std::string>) {
      if (is_text_type(data_type) || data_type == DataType::CHAR) {
        return GeneratedValue::text(typed_value);
      }
      int year = 0;
      int month = 0;
      int day = 0;
      int hour = 0;
      int minute = 0;
      int second = 0;
      int consumed = 0;
      if (data_type == DataType::DATE &&
          std::sscanf(typed_value.c_str(), "%d-%d-%d%n", &year, &month, &day, &consumed) == 3 &&
          consumed == static_cast<int>(typed_value.size()) &&
          std::chrono::year_month_day{std::chrono::year{year},
                                      std::chrono::month{static_cast<unsigned int>(month)},
                                      std::chrono::day{static_cast<unsigned int>(day)}}.ok()) {
        return GeneratedValue::date(DateValue{year, month, day});
      }
      if (data_type == DataType::TIME &&
          std::sscanf(typed_value.c_str(), "%d:%d:%d%n", &hour, &minute, &second, &consumed) == 3 &&
          consumed == static_cast<int>(typed_value.size()) && hour >= 0 && hour < 24 &&
          minute >= 0 && minute < 60 && second >= 0 && second < 60) {
        return GeneratedValue::time(TimeValue{hour, minute, second});
      }
      if (data_type == DataType::DATETIME &&
          std::sscanf(typed_value.c_str(), "%d-%d-%d %d:%d:%d%n", &year, &month, &day, &hour,
                      &minute, &second, &consumed) == 6 &&
          consumed == static_cast<int>(typed_value.size()) && hour >= 0 && hour < 24 &&
          minute >= 0 && minute < 60 && second >= 0 && second < 60 &&
          std::chrono::year_month_day{std::chrono::year{year},
                                      std::chrono::month{static_cast<unsigned int>(month)},
                                      std::chrono::day{static_cast<unsigned int>(day)}}.ok()) {
        return GeneratedValue::date_time(
            DateTimeValue{DateValue{year, month, day}, TimeValue{hour, minute, second}});
      }
    }
    return std::nullopt;
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

EffectiveCheckConstraint ColumnDomainResolver::effective_check_for_column(
    const Table& table, const Column& column, const GenerationConfig& config) {
  EffectiveCheckConstraint effective = effective_check_for_column(table, column);
  const ColumnGenerationConfig* column_config =
      config.get_column_config(table.get_table_name(), column.get_column_name());
  if (column_config == nullptr) {
    return effective;
  }

  if (column_config->has_values) {
    effective.allowed_values.clear();
    for (const auto& value : column_config->values) {
      if (auto coerced = coerce_configured_value(value, column.get_column_type().data_type);
          coerced.has_value()) {
        effective.allowed_values.push_back(std::move(coerced.value()));
      }
    }
  }
  if (column_config->min_value.has_value()) {
    if (!effective.min_value.has_value() ||
        column_config->min_value.value() > effective.min_value.value()) {
      effective.min_value = column_config->min_value;
      effective.min_inclusive = true;
    }
  }
  if (column_config->max_value.has_value()) {
    if (!effective.max_value.has_value() ||
        column_config->max_value.value() < effective.max_value.value()) {
      effective.max_value = column_config->max_value;
      effective.max_inclusive = true;
    }
  }
  return effective;
}

ColumnDomain ColumnDomainResolver::domain_for_column(const Table* table, const Column* column,
                                                     const GenerationConfig& config,
                                                     int requested_rows) {
  (void)requested_rows;

  ColumnDomain domain{.check = effective_check_for_column(*table, *column, config),
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
