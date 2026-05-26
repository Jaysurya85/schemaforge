#pragma once
#include <random>

namespace schemaforge {

class RandomEngine {
 private:
  std::mt19937_64 rng;

 public:
  explicit RandomEngine(unsigned int seed) : rng(seed) {}

  int next_int(int min, int max) {
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(rng);
  }

  double next_decimal(double min, double max) {
    std::uniform_real_distribution<double> distribution(min, max);
    return distribution(rng);
  }

  bool next_bool() {
    std::bernoulli_distribution distribution(0.5);
    return distribution(rng);
  }
};

}  // namespace schemaforge
