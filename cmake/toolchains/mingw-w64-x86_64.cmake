# Linux-hosted x86_64-w64-mingw32 toolchain for producing Windows binaries.
#
# This file is intended to be used through vcpkg's chainload hook:
#
#   -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake
#   -DVCPKG_TARGET_TRIPLET=x64-mingw-static
#   -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-x86_64.cmake

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc CACHE FILEPATH "MinGW-w64 C compiler")
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++ CACHE FILEPATH "MinGW-w64 C++ compiler")
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres CACHE FILEPATH "MinGW-w64 resource compiler")

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -static-libgcc -static-libstdc++")
