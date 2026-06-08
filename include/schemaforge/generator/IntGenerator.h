#pragma once
#include <vector>

#include "schemaforge/generator/GeneratedValue.h"

namespace schemaforge {

class IntGenerator {
 private:
  int max;

 public:
  int min;
  IntGenerator() : max(-1), min(0) {};
  IntGenerator(int min) : max(-1), min(min) {};
  IntGenerator(int min, int max) : max(max), min(min) {};
  std::vector<GeneratedValue> generate(int size);
};
}  // namespace schemaforge
