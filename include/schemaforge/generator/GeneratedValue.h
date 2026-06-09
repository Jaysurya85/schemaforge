#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace schemaforge {

struct DateValue {
  int year;
  int month;
  int day;
};

struct TimeValue {
  int hour;
  int minute;
  int second;
};

struct DateTimeValue {
  DateValue date;
  TimeValue time;
};

class GeneratedValue {
 private:
  using Value =
      std::variant<std::monostate, std::int64_t, double, std::string, bool, DateValue, TimeValue,
                   DateTimeValue>;

  Value value;

  explicit GeneratedValue(Value value) : value(std::move(value)) {}

 public:
  GeneratedValue() = delete;

  static GeneratedValue null() {
    return GeneratedValue(Value{std::monostate{}});
  }

  static GeneratedValue integer(std::int64_t value) {
    return GeneratedValue(Value{value});
  }

  static GeneratedValue numeric(double value) {
    return GeneratedValue(Value{value});
  }

  static GeneratedValue text(std::string value) {
    return GeneratedValue(Value{std::move(value)});
  }

  static GeneratedValue boolean(bool value) {
    return GeneratedValue(Value{value});
  }

  static GeneratedValue date(DateValue value) {
    return GeneratedValue(Value{value});
  }

  static GeneratedValue time(TimeValue value) {
    return GeneratedValue(Value{value});
  }

  static GeneratedValue date_time(DateTimeValue value) {
    return GeneratedValue(Value{value});
  }

  template <typename Visitor>
  decltype(auto) visit(Visitor&& visitor) const {
    return std::visit(std::forward<Visitor>(visitor), value);
  }
};

inline std::ostream& operator<<(std::ostream& os, const GeneratedValue& value) {
  value.visit([&os](const auto& typed_value) {
    using ValueType = std::decay_t<decltype(typed_value)>;
    if constexpr (std::is_same_v<ValueType, bool>) {
      os << (typed_value ? "true" : "false");
    } else if constexpr (std::is_same_v<ValueType, std::monostate>) {
      os << "null";
    } else if constexpr (std::is_same_v<ValueType, DateValue>) {
      os << typed_value.year << '-' << typed_value.month << '-' << typed_value.day;
    } else if constexpr (std::is_same_v<ValueType, TimeValue>) {
      os << typed_value.hour << ':' << typed_value.minute << ':' << typed_value.second;
    } else if constexpr (std::is_same_v<ValueType, DateTimeValue>) {
      os << typed_value.date.year << '-' << typed_value.date.month << '-' << typed_value.date.day
         << ' ' << typed_value.time.hour << ':' << typed_value.time.minute << ':'
         << typed_value.time.second;
    } else {
      os << typed_value;
    }
  });
  return os;
}

}  // namespace schemaforge
