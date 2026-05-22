cmake_minimum_required(VERSION 3.20)

project(schemaforge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(schemaforge
    src/main.cpp
    src/parser/ParserAdapter.cpp
    src/schema/Table.cpp
    src/schema/Column.cpp
    src/validation/SchemaValidator.cpp
    src/graph/DependencyGraph.cpp
    src/io/FileReader.cpp
)

target_include_directories(schemaforge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/sql-parser/src ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(schemaforge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/sql-parser/libsqlparser.so
)
