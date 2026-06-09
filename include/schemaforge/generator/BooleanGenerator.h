#pragma once
#include <vector>

#include "schemaforge/generator/GeneratedValue.h"

namespace schemaforge {

class BooleanGenerator {
 public:
  BooleanGenerator() = default;
  std::vector<GeneratedValue> generate(int size);
};
}  // namespace schemaforge
