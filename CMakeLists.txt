cmake_minimum_required(VERSION 3.20)

# Suppress Boost-related CMake warnings
cmake_policy(SET CMP0144 NEW)
cmake_policy(SET CMP0167 NEW)
set(Boost_NO_WARN_NEW_VERSIONS ON)

file(STRINGS "VERSION" PROJECT_VERSION_STRING LIMIT_COUNT 1)
string(STRIP "${PROJECT_VERSION_STRING}" PROJECT_VERSION_STRING)

project(mail VERSION ${PROJECT_VERSION_STRING} LANGUAGES CXX)

project(mail LANGUAGES CXX)

# Enable C++23
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Seastar REQUIRED)

add_executable(smtp-server src/main.cpp)

target_link_libraries(smtp-server PRIVATE Seastar::seastar)

target_include_directories(smtp-server PRIVATE
    include
)

# TODO: Defer binary optimizations
target_compile_options(smtp-server PRIVATE
    -Wall -Wextra
)

target_compile_definitions(smtp-server PRIVATE
  PROJECT_VERSION="${PROJECT_VERSION_STRING}"
)