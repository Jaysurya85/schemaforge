#pragma once
#include <string>
#include <vector>

namespace schemaforge {
using Data = std::string;

class IntGenerator {
 private:
  int max;

 public:
  int min;
  IntGenerator() : max(-1), min(0) {};
  IntGenerator(int min) : max(-1), min(min) {};
  IntGenerator(int min, int max) : max(max), min(min) {};
  std::vector<Data> generate(int size);
};
}  // namespace schemaforge
