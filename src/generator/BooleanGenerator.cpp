#include "schemaforge/generator/BooleanGenerator.h"

namespace schemaforge {

std::vector<GeneratedValue> BooleanGenerator::generate(int size) {
  std::vector<GeneratedValue> result;
  result.reserve(size);
  for (int row = 0; row < size; ++row) {
    result.push_back(GeneratedValue::boolean(row % 2 == 0));
  }
  return result;
}

}  // namespace schemaforge
