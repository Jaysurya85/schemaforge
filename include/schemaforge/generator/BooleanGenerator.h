#pragma once
#include <string>
#include <vector>

namespace schemaforge {
using Data = std::string;

class BooleanGenerator {
 public:
  BooleanGenerator() = default;
  std::vector<Data> generate(int size);
};
}  // namespace schemaforge
