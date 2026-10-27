#!/usr/bin/env bash
# Builds SDL3-uwp as a shared library with MSVC under Wine and installs it into
# build/sdl3-prefix.
#
# SDL3-uwp's WinRT code is C++/CX, which only MSVC compiles. The patches in
# patches/sdl3-uwp are applied in a separate worktree so the submodule stays at
# the upstream commit.
#
# Environment: MSVC_ROOT (default ~/.local/share/msvc), see cmake/msvc-toolchain.cmake.
set -euo pipefail

repo=$(realpath "$(dirname "$0")/..")
sdl_repo="$repo/thirdparty/SDL3-uwp"
src="$repo/build/sdl3-src"
build="$repo/build/sdl3-build"
prefix="$repo/build/sdl3-prefix"

if [ ! -d "$src" ]; then
  git -C "$sdl_repo" worktree add --detach "$src" HEAD
  git -C "$src" -c user.name=build -c user.email=build@localhost am -q "$repo"/patches/sdl3-uwp/*.patch
fi

cmake -S "$src" -B "$build" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/msvc-toolchain.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF \
  -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF -DSDL_GPU=OFF -DSDL_CAMERA=OFF
cmake --build "$build"
cmake --install "$build"
echo "$prefix"
