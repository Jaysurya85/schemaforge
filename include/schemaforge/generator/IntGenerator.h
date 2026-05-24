#pragma once
#include <string>
#include <vector>

namespace schemaforge {
using Data = std::string;

class IntGenerator {
 public:
  int min;
  IntGenerator() : min(0) {};
  IntGenerator(int min) : min(min) {};
  std::vector<Data> generate(int size);
};
}  // namespace schemaforge
