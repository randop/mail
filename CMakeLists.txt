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

if(CMAKE_BUILD_TYPE STREQUAL "Release")
  include(CheckIPOSupported)
  check_ipo_supported(RESULT ipo_supported)
  if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
  endif()

  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
      -O3
      -march=x86-64
      -mtune=generic
      -funroll-loops
      -fomit-frame-pointer
      -fno-plt
      -fvisibility=hidden
    )

    add_link_options(
      -Wl,-O1
      -Wl,--as-needed
      -Wl,--strip-all
    )
  endif()

  # Sanitizes and disables assert(); standard
  add_compile_definitions(
    NDEBUG
  )
endif()

find_package(Seastar REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(smtp-server 
  src/main.cpp
  src/email_helpers.cpp
  src/file_helpers.cpp
  src/ip_helpers.cpp
  src/x509_helpers.cpp
  src/uuid_helpers.cpp
)

target_link_libraries(smtp-server PRIVATE Seastar::seastar)
target_link_libraries(smtp-server
  PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_include_directories(smtp-server PRIVATE
    include
)

target_compile_options(smtp-server PRIVATE
    -Wall -Wextra
)

target_compile_definitions(smtp-server PRIVATE
  PROJECT_VERSION="${PROJECT_VERSION_STRING}"
)