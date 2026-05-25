#include "schemaforge/generator/BooleanGenerator.h"

namespace schemaforge {

std::vector<Data> BooleanGenerator::generate(int size) {
  std::vector<Data> result;
  result.reserve(size);
  for (int row = 0; row < size; ++row) {
    result.emplace_back(row % 2 == 0 ? "true" : "false");
  }
  return result;
}

}  // namespace schemaforge
