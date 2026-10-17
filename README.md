# stream-bits

A high-performance HTTP server and framework written from scratch in C++23, with no third-party
libraries for networking, protocol parsing or the framework layer.

stream-bits is a **library**. It ships as a shared library by default (a static build is
optional), along with its headers and a CMake package. Runnable programs are in `examples/`.

## Requirements

- CMake **3.25+** (presets) or **3.21+** (plain CMake)
- Ninja (bundled with Visual Studio and CLion; `apt install ninja-build` / `brew install ninja`)
- A C++23 compiler with `std::expected`:
  - MSVC from Visual Studio 2022 17.3+
  - clang-cl from Visual Studio (the standalone LLVM may be too old for the VS standard library)
  - GCC 12+
  - Clang 16+

## Building

### With presets (recommended)

| Preset                                  | Compiler                        | Platform                    |
|-----------------------------------------|---------------------------------|-----------------------------|
| `msvc-debug` / `msvc-release`           | MSVC `cl`                       | Windows                     |
| `clang-cl-debug` / `clang-cl-release`   | clang-cl bundled with VS        | Windows                     |
| `gcc-debug` / `gcc-release`             | `g++`                           | Linux, MinGW                |
| `clang-debug` / `clang-release`         | `clang++`                       | Linux, macOS                |

Run `cmake --list-presets=all` to see them all.

On Windows, the `msvc-*` and `clang-cl-*` presets must run from a **Developer PowerShell / Developer
Command Prompt for VS**, which provides the compiler, the Windows SDK and `VCINSTALLDIR`.

Configure and build in one step:

```sh
cmake --workflow --preset msvc-release
```

Or one step at a time:

```sh
cmake --preset gcc-debug
cmake --build --preset gcc-debug
```

Intermediate files go to `build/<preset>/`; the final executables and DLLs go to `bin/` at the project root.

### Without presets

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

`--config` applies to multi-config generators such as Visual Studio. `CMAKE_BUILD_TYPE` applies to
Ninja and Make.

### Options

| Option                       | Default                           | Effect                                     |
|------------------------------|-----------------------------------|--------------------------------------------|
| `BUILD_SHARED_LIBS`          | `ON`                              | `OFF` builds a static library              |
| `STREAM_BITS_BUILD_EXAMPLES` | `ON` only for the top-level build | Builds the programs in `examples/`         |

Pass them at configure time, e.g. `cmake --preset gcc-release -DBUILD_SHARED_LIBS=OFF`.

## Running the examples

Examples are built along with the library:

```sh
./bin/helloworld-server
```

The library and the examples are both written to `bin/`, so `stream-bits.dll` is already next to the executable.
`bin/` is shared by all presets: building another preset overwrites its files.

## Installing

```sh
cmake --install build/msvc-release                         # to install/msvc-release/
cmake --install build/msvc-release --prefix C:/libs/stream-bits
```

The install tree contains:

```
bin/        stream-bits.dll                    (Windows, shared build)
lib/        stream-bits.lib / libstream-bits.so / .a
include/    public headers
lib/cmake/stream-bits/                         CMake package for find_package
```

## Using stream-bits in your project

All three methods give you the same target, `stream-bits::stream-bits`. Linking it adds the
include paths and the C++23 requirement.

### 1. An installed package (`find_package`)

```cmake
find_package(stream-bits 0.1 REQUIRED CONFIG)
target_link_libraries(my_app PRIVATE stream-bits::stream-bits)
```

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/libs/stream-bits
```

### 2. Source in your tree (`add_subdirectory`)

```cmake
add_subdirectory(third_party/stream-bits)
target_link_libraries(my_app PRIVATE stream-bits::stream-bits)
```

### 3. Downloaded at configure time (`FetchContent`)

```cmake
include(FetchContent)
FetchContent_Declare(stream-bits
        GIT_REPOSITORY <repository-url>
        GIT_TAG        <tag-or-commit>
)
FetchContent_MakeAvailable(stream-bits)
target_link_libraries(my_app PRIVATE stream-bits::stream-bits)
```

Examples are not built when stream-bits is pulled in with methods 2 or 3.

### Windows and the shared library

Windows has no rpath, so `stream-bits.dll` must be next to your executable or on `PATH`. A
post-build copy does this:

```cmake
add_custom_command(TARGET my_app POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_RUNTIME_DLLS:my_app> $<TARGET_FILE_DIR:my_app>
        COMMAND_EXPAND_LISTS
)
```

Or build stream-bits with `-DBUILD_SHARED_LIBS=OFF`.

### Building an example as a standalone consumer

Each example is also a standalone CMake project. Building it against an installed stream-bits
checks that the package works for third parties:

```sh
cmake --workflow --preset gcc-release
cmake --install build/gcc-release
cmake -S examples/01-helloworld-server -B build/example -DCMAKE_PREFIX_PATH="$PWD/install/gcc-release"
cmake --build build/example
```

## CLion

CLion loads `CMakePresets.json` as CMake profiles (Settings → Build, Execution, Deployment → CMake).
For the `msvc-*` / `clang-cl-*` profiles, select the **Visual Studio** toolchain. For `gcc-*` on
Windows, select **MinGW**. The shared run configuration `helloworld-server` runs the first example.

## Adding files

Sources are listed by hand in the root `CMakeLists.txt`, so a new file must be registered there.

- **Library source**: create the file under `src/`, then add its path to `add_library(stream-bits ...)`.
  Platform-specific files go inside the matching `if (WIN32)` / `elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")`
  block (`target_sources(stream-bits PRIVATE ...)`). After editing, CMake re-runs by itself on the next build.
- **Public header**: create `include/stream-bits/<name>.hpp`, used as `#include <stream-bits/<name>.hpp>`.
  Internal layers such as `include/net/` follow the same rule: `#include "net/<name>.hpp"`.
- **New example**: create `examples/<NN-name>/CMakeLists.txt` and `main.cpp`, copying `01-helloworld-server`.
  `examples/CMakeLists.txt` finds it on its own (no edit needed); the executable appears in `bin/`.

## Project layout

```
include/stream-bits/   public headers
include/net/           networking layer headers
src/                   library sources (platform backends in src/net/)
examples/              runnable examples, each a standalone CMake project
bin/                   final executables and DLLs (generated, git-ignored)
cmake/                 CMake package config
```
