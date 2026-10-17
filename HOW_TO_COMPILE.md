# How to Compile

Step-by-step guide to building stream-bits on **Windows** with the presets from `CMakePresets.json`.
For the general overview (requirements, Linux/macOS, consuming the package), see `README.md`.

---

## 1. Prerequisites

| Tool                  | Minimum                         | Check with             |
|-----------------------|---------------------------------|------------------------|
| CMake                 | 3.25 (preset schema v6)         | `cmake --version`      |
| Ninja                 | any recent                      | `ninja --version`      |
| Visual Studio         | 2022 17.3+ (2026 recommended)   | Visual Studio Installer |

In the **Visual Studio Installer**, the workload *"Desktop development with C++"* must be installed.
To use Clang, also tick the individual component
**"C++ Clang Compiler for Windows"** (and *"MSBuild support for LLVM (clang-cl) toolset"*).

> **Do not use a standalone LLVM from `C:\Program Files\LLVM`** unless it is recent (Clang 19+).
> The project needs C++23 (`std::expected`), and an old Clang cannot compile it nor parse the
> Visual Studio standard library headers. The Clang bundled with Visual Studio is always matched
> to its STL, so prefer it.

The Clang bundled with Visual Studio lives at:

```
<VS install dir>\VC\Tools\Llvm\x64\bin\clang-cl.exe   (MSVC-style flags: /W4, /std:...)
<VS install dir>\VC\Tools\Llvm\x64\bin\clang++.exe    (GCC-style flags: -Wall, -std=...)
```

e.g. `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\Llvm\x64\bin\`.

---

## 2. Open a Developer shell (mandatory)

The presets use the **Ninja** generator. Unlike the Visual Studio generator, Ninja does **not**
locate the compiler on its own: it expects `cl.exe`, the Windows SDK headers/libs and the variable
`VCINSTALLDIR` to already be in the environment. A normal terminal does not have them.

Pick one:

- **Start menu** → *"Developer PowerShell for VS 2026"* (or *"x64 Native Tools Command Prompt"*).
- **From an existing PowerShell**:

  ```powershell
  & "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
  ```

  (A warning saying `vswhere.exe` is not recognized can be ignored.)

- **From cmd.exe**:

  ```bat
  "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
  ```

Verify it worked:

```powershell
$env:VCINSTALLDIR      # must print ...\VC\
where.exe cl           # must find cl.exe
```

> If more than one Visual Studio is installed (e.g. VS 2026 and VS 2022 Build Tools), the shell
> you open decides which toolset is used. Use `-Arch amd64` so you get the 64-bit compiler.

Then go to the project root:

```powershell
cd C:\path\to\stream-bits-server
```

---

## 3. Choose a preset

```powershell
cmake --list-presets
```

| Preset                                | Compiler                                   | Works on Windows?            |
|---------------------------------------|--------------------------------------------|------------------------------|
| `msvc-debug` / `msvc-release`         | MSVC `cl.exe`                              | Yes (Developer shell)        |
| `clang-cl-debug` / `clang-cl-release` | VS-bundled `clang-cl.exe`                  | Yes (Developer shell)        |
| `clang-debug` / `clang-release`       | `clang++` **from PATH**                    | Only with an override (§5)   |
| `gcc-debug` / `gcc-release`           | `g++` from PATH                            | Only with MinGW installed    |

---

## 4. Build

### MSVC

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug
```

### Visual Studio's Clang (clang-cl) — recommended way to use Clang

```powershell
cmake --preset clang-cl-debug
cmake --build --preset clang-cl-debug
```

This preset already points to Visual Studio's Clang: its compiler is
`$env{VCINSTALLDIR}Tools/Llvm/x64/bin/clang-cl.exe`. That is why it **only works inside the
Developer shell** — outside it `VCINSTALLDIR` is empty and the path breaks.

### Release builds in one command

The `*-release` presets also have a workflow (configure + build):

```powershell
cmake --workflow --preset msvc-release
cmake --workflow --preset clang-cl-release
```

Intermediate files go to `build/<preset>/`; executables and DLLs go to `bin/`.

---

## 5. Using Visual Studio's `clang++` (GCC-style driver)

The `clang-*` presets call `clang++` from `PATH`, which may resolve to an old standalone LLVM.
To point them at Visual Studio's `clang++.exe` instead, override the compiler **when configuring**
(still inside the Developer shell):

```powershell
cmake --preset clang-debug "-DCMAKE_CXX_COMPILER=$env:VCINSTALLDIR\Tools\Llvm\x64\bin\clang++.exe"
cmake --build --preset clang-debug
```

The override is stored in the cache, so later `cmake --build` calls keep using it. If you change
compilers for an existing build directory, delete `build/clang-debug/` first.

To avoid typing it every time, create a **`CMakeUserPresets.json`** (git-ignored, per developer)
next to `CMakePresets.json`:

```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "vs-clang-debug",
      "displayName": "VS clang++ Debug",
      "inherits": "clang-debug",
      "cacheVariables": {
        "CMAKE_CXX_COMPILER": "$env{VCINSTALLDIR}Tools/Llvm/x64/bin/clang++.exe"
      }
    }
  ],
  "buildPresets": [
    { "name": "vs-clang-debug", "configurePreset": "vs-clang-debug" }
  ]
}
```

```powershell
cmake --preset vs-clang-debug
cmake --build --preset vs-clang-debug
```

**clang-cl vs clang++:** both are the same Clang. `clang-cl` accepts MSVC flags and is what the
project's `CMakeLists.txt` treats as the MSVC frontend (`/W4 /permissive-`); `clang++` takes
GCC-style flags (`-Wall -Wextra -Wpedantic`). Both link against the MSVC STL and runtime.

---

## 6. Run the example

```powershell
.\bin\helloworld-server.exe
```

`stream-bits` is built as a DLL by default. Both the DLL and the example are written to `bin/`,
so the `.exe` finds it without any extra step.

For a static library instead, add `-DBUILD_SHARED_LIBS=OFF` when configuring.

---

## 7. Install (optional)

```powershell
cmake --install build/msvc-release
```

Files go to `install/msvc-release/` (headers, library and the CMake package for `find_package`).

---

## 8. CLion

CLion loads `CMakePresets.json` automatically as CMake profiles, but it still needs the Visual
Studio environment:

1. **Settings → Build, Execution, Deployment → Toolchains** → add a **Visual Studio** toolchain
   pointing to the VS install, architecture `amd64`.
2. Move it to the **top** of the list (the default toolchain is the one preset profiles use), or
   select it explicitly in each profile under **CMake**.
3. Enable the preset profiles you want under **Settings → … → CMake**.

CLion then runs `vcvarsall` for you, so `cl` and `VCINSTALLDIR` are available.

---

## 9. Troubleshooting

| Symptom                                                         | Cause / Fix                                                                 |
|-----------------------------------------------------------------|-----------------------------------------------------------------------------|
| `CMAKE_CXX_COMPILER: cl is not a full path and was not found`   | Not in a Developer shell (§2).                                              |
| `clang-cl` preset fails with a relative path `Tools/Llvm/...`   | `VCINSTALLDIR` is empty → not in a Developer shell (§2).                    |
| `clang-cl.exe` not found under `VC\Tools\Llvm`                  | Install the *"C++ Clang Compiler for Windows"* component (§1).              |
| Errors about `std::expected` / STL headers with `clang-*`       | `clang++` from PATH is an old standalone LLVM → use §5.                     |
| `cannot find ... kernel32.lib` / missing Windows headers        | Wrong shell, or the Windows SDK is not installed.                           |
| Changed compiler but the old one is still used                  | The compiler is cached: delete `build/<preset>/` and configure again.       |
| `.exe` fails to start: `stream-bits.dll` not found              | Run from `bin/`, or copy the DLL next to the `.exe`.                |
