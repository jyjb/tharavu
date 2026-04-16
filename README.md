# Tharavu Data Engine

Created by Jeyaraj

## Lightweight data engine for tables, vocabularies, and embeddings

Tharavu Data Engine is a hybrid open-source project that combines a native
C static/shared library with command-line helpers for managing tabular
data, vocabulary hash tables, and float embeddings.

## Key Features

- Portable binary formats for `ODAT`, `OVOC`, and `OVEC`
- C/C++ friendly public API with opaque handles
- Shared library bindings for C#, F#, Python, Java, and other languages
- Build-time support for both static and shared library distributions
- Automatic config initialization and base data directory creation
- Sample data generation for quick evaluation

## Supported Platforms

- Windows (MinGW, MSVC)
- Linux
- macOS

## Supported Languages

- C
- C++
- C#
- F#
- Python
- Java
- Rust

## Installation

Clone the repository and install the toolchain for your platform.

### Windows

- Install MinGW-w64 or MSVC
- Ensure `gcc` or `cl` is on your path

### Linux / macOS

- Install GCC or Clang
- Install `make` and `cmake` if needed

## Build

### Using GNU Make

```bash
make
```

This builds the static library, shared library, and available CLI tools.

### Using CMake

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Install

```bash
cmake --install build --prefix ./dist
```

## Quick Start

```bash
# Generate default config and data folder
# run a small sample app or the library via your chosen binding
```

The library creates `tharavu.ini` and the configured base data directory
if they do not already exist.

## Usage

See `USAGE.md` for detailed examples in C, C++, C#, F#, Python, and Java.

## Contribution Guidelines

We welcome contributions that improve reliability, portability, and
language bindings.

- Open an issue before implementing large features.
- Keep pull requests focused and small.
- Include tests or examples when adding functionality.
- Keep the project compatible with GPL v3.

## License & Usage (Important)

- This project is open-source and free to use.
- Commercial use is allowed.
- If you modify and distribute this software, you MUST release your
  changes under the same GNU GPL v3 license.
- Copyleft means that any distributed derivative work must remain
  licensed under GPL v3, so recipients keep the same freedoms.

If you only use the software internally and do not distribute it, you are
not required to publish your changes.  Once you share a modified build
with others, the GPL requires that the source remain available under the
same license.

## Project Type

Hybrid: shared library + executable utilities.

## Repository Layout

- `include/` — headers
- `src/` — engine and wrapper source code
- `tools/` — CLI helper source code
- `sample/` — sample data and sample generator
- `CMakeLists.txt`, `Makefile` — build scripts
- `LICENSE`, `NOTICE`, `CONTRIBUTORS.md` — open-source governance files

## Contact

Please keep contributions aligned with GPL v3 and include a clear
change description in each pull request.
