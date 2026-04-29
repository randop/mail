#!/bin/sh

set -eu

find src -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -o -name "*.c" \) -exec clang-format -i {} \+

find include -type f \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" -o -name "*.c" -o -name "*.cc" \) -exec clang-format -i {} \+
