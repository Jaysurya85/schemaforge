#pragma once

#include <string>

namespace schemaforge {

class FileReader {
public:
  static std::string read_file(const std::string &file_path);
};

} // namespace schemaforge
