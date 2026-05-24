#include "schemaforge/generator/IntGenerator.h"

namespace schemaforge {

std::vector<Data> IntGenerator::generate(int size) {
  std::vector<Data> result;
  result.reserve(size);
  for (int i = 0; i < size; ++i) {
    result.push_back(std::to_string(i + min));
  }
  return result;
};

}  // namespace schemaforge
