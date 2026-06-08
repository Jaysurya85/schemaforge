#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

namespace schemaforge {

class GeneratedValue {
 private:
  using Value = std::variant<std::int64_t, double, std::string, bool>;

  Value value;

  explicit GeneratedValue(Value value) : value(std::move(value)) {}

 public:
  GeneratedValue() = delete;

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
    } else {
      os << typed_value;
    }
  });
  return os;
}

}  // namespace schemaforge
