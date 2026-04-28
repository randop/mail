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
export CMAKE_PREFIX_PATH="${OPT_PREFIX}/liburing/current:${CMAKE_PREFIX_PATH}"

### ***verbose***
# echo "CMAKE_PREFIX_PATH: $CMAKE_PREFIX_PATH"
###

LOCAL_LIBRARY_PATH=${HOME}/opt/boost/current/lib

if [ -n "${LD_LIBRARY_PATH+set}" ]; then
  export LD_LIBRARY_PATH="${LOCAL_LIBRARY_PATH}:${LD_LIBRARY_PATH}"
else
  export LD_LIBRARY_PATH=$LOCAL_LIBRARY_PATH
fi

mkdir -p .build
cd .build

detect_ubuntu_jammy() {
  if [ -f /etc/os-release ]; then
    . /etc/os-release
    [ "$ID" = "ubuntu" ] && [ "$VERSION_ID" = "22.04" ]
    return $?
  fi

  if [ -f /etc/lsb-release ]; then
    . /etc/lsb-release
    [ "$DISTRIB_ID" = "Ubuntu" ] && [ "$DISTRIB_RELEASE" = "22.04" ]
    return $?
  fi

  if [ -f /etc/issue ]; then
    case $(cat /etc/issue) in
    *"Ubuntu 22.04"*) return 0 ;;
    esac
    return 1
  fi

  return 2
}

if detect_ubuntu_jammy; then
  echo "Ubuntu 22.04 LTS (Jammy Jellyfish) detected."
  export CC=gcc-13
  export CXX=g++-13
  cmake .. -DCMAKE_C_COMPILER=/usr/bin/gcc-13 -DCMAKE_CXX_COMPILER=/usr/bin/g++-13 -DCMAKE_BUILD_TYPE=RelWithDebInfo
else
  cmake .. -DCMAKE_BUILD_TYPE=RelWithDebInfo
fi

make -j$(nproc)