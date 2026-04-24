#!/bin/sh

set -eu

export OPT_PREFIX="$HOME/opt"
export PKG_CONFIG_PATH="$HOME/.local/lib/pkgconfig"
export BOOST_ROOT="$HOME/opt/boost/current"
export CMAKE_PREFIX_PATH="$HOME/opt/seastar/current"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/c-ares/current:$CMAKE_PREFIX_PATH"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/boost/current:$CMAKE_PREFIX_PATH"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/fmt/current:${CMAKE_PREFIX_PATH}"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/hwloc/current:${CMAKE_PREFIX_PATH}"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/protobuf/current:${CMAKE_PREFIX_PATH}"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/lksctp/current:${CMAKE_PREFIX_PATH}"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/colm-suite/current:${CMAKE_PREFIX_PATH}"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/valgrind/current:${CMAKE_PREFIX_PATH}"
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/yamlcpp/current:${CMAKE_PREFIX_PATH}"

### ***verbose***
# echo "CMAKE_PREFIX_PATH: $CMAKE_PREFIX_PATH"
###

LOCAL_LIBRARY_PATH=${HOME}/opt/boost/current/lib

if [ -n "${LD_LIBRARY_PATH+set}" ]; then
  export LD_LIBRARY_PATH="${LOCAL_LIBRARY_PATH}:${LD_LIBRARY_PATH}"
else
  export LD_LIBRARY_PATH=$LOCAL_LIBRARY_PATH
fi

clang-format -i src/main.cpp

mkdir -p .dist
cd .dist
cmake .. -DCMAKE_BUILD_TYPE=Release

make -j$(nproc)