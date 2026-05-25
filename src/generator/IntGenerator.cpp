#include "schemaforge/generator/IntGenerator.h"

namespace schemaforge {

std::vector<Data> IntGenerator::generate(int size) {
  std::vector<Data> result;
  result.reserve(size);

  const bool should_wrap = max >= min;
  const int range_size = max - min + 1;

  for (int i = 0; i < size; ++i) {
    if (should_wrap) {
      result.push_back(std::to_string(min + (i % range_size)));
      continue;
    }

    result.push_back(std::to_string(min + i));
  }
  return result;
};

}  // namespace schemaforge
