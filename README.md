# Tharavu Data Engine

> **Created by Jeyaraj**

*"tharavu" (தரவு) — Tamil: data*

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

---

## High-Performance Binary Data Engine for Tables, Vocabularies, and Embeddings

Tharavu is an open-source shared library (DLL / SO) and a set of CLI
utilities for managing persistent binary data without a database server.
It provides three purpose-built binary formats — typed tabular data,
vocabulary hash tables, and dense float embedding vectors — all accessed
through a clean C ABI that works from any language with an FFI.

Tharavu is the storage and I/O foundation for the
[Sorkuvai](https://github.com/jeyaraj/sorkuvai) NLP engine and any
other project that needs fast, portable binary data files.

---

## Key Features

| Feature | Detail |
|---|---|
| **Three binary formats** | ODAT (typed rows/columns) · OVOC (O(1) vocab hash) · OVEC (fixed-stride float vectors) |
| **mmap zero-copy access** | Vector and vocab lookups return direct mmap pointers — no allocation per call |
| **Top-k vector search** | Brute-force cosine similarity scan over an entire `.ovec` store |
| **Full CRUD on ODAT** | Insert, update, delete rows; atomic save (write-then-rename) |
| **Batch operations** | Bulk vocab lookups and bulk vector fetches for high-throughput pipelines |
| **Logical names** | `"db.table"` → `{data_dir}/db/table.ext` with automatic directory creation |
| **Multi-process safe writes** | Sidecar `.lock` file with `LockFileEx` (Windows) / `fcntl F_SETLK` (POSIX) on all three formats |
| **Thread-safe errors** | `tde_last_error()` uses thread-local storage |
| **Clean C ABI** | Callable from C, C++, C#, F#, Python, Java, Rust, and any FFI runtime |
| **CLI tools** | `de_crud.exe` for ad-hoc inspection and data entry |

---

## Supported Platforms

| Platform | Toolchain | Status |
|---|---|---|
| Windows 10 / 11 | MinGW-w64 (GCC) | Tested |
| Windows 10 / 11 | MSVC | Supported |
| Linux | GCC / Clang | Supported |
| macOS | Clang | Supported |

## Supported Languages

**C · C++ · C# · F# · Python · Java · Rust** — any language with a foreign function interface

---

## Repository Layout

```
Data Engine/
├── src/
│   ├── tharavu_dll.c       DLL entry points (tde_* wrappers)
│   ├── data_engine.c       Core engine implementation
│   ├── platform.c          OS abstraction (mmap, locking, endian)
│   └── ...
├── include/
│   ├── tharavu_dll.h       Public API header — the only file consumers include
│   ├── data_engine.h       Internal engine header
│   ├── tharavu_types.h     Shared type and error constants
│   └── formats.h           Binary format magic and layout constants
├── tools/
│   └── de_crud.c           CLI helper source
├── sample/
│   └── ...                 Sample data generator
├── tharavu.dll             Built shared library
├── libtharavu_dll.a        Windows import library
├── tharavu.ini             Default configuration
├── Makefile
├── CMakeLists.txt
├── README.md
├── USAGE.md
├── API.md
├── LICENSE
├── NOTICE
├── CONTRIBUTORS.md
└── CODE_OF_CONDUCT.md
```

---

## Dependencies

Standard C99 runtime and `libm`.  No external libraries required.

---

## Installation

```bash
git clone https://github.com/jeyaraj/tharavu-data-engine.git
cd "tharavu-data-engine"
```

Ensure GCC (MinGW-w64 on Windows) or Clang is installed and on `PATH`.

---

## Build

### Using Make (recommended)

```bash
make            # tharavu.dll + libtharavu_dll.a + CLI tools
make clean      # remove all build artifacts
```

### Using CMake

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix ./dist
```

### Linux / macOS

```bash
make            # produces libtharavu.so
```

---

## Quick Start (C)

```c
#include "tharavu_dll.h"
#include <stdio.h>

int main(void) {
    tde_config_load("tharavu.ini");

    /* Create an in-memory table */
    const char *cols[] = {"id", "word", "score"};
    tde_handle_t t = tde_create(cols, 3);

    /* Insert a row */
    tde_handle_t row = tde_row_begin(t);
    tde_row_set_int32  (row, 0, 1);
    tde_row_set_string (row, 1, "transformer");
    tde_row_set_float32(row, 2, 0.95f);
    tde_row_commit(row);

    /* Save using a logical name */
    tde_save_logical(t, "demo.words");
    tde_close(t);

    /* Reload and read back */
    t = tde_open_odat("demo.words");
    char word[64] = {0};
    tde_get_string(t, 0, 1, word, sizeof(word));
    printf("word = %s  rows = %d\n", word, tde_row_count(t));
    tde_close(t);
    return 0;
}
```

**Build:**
```bash
gcc main.c -std=c99 -I include -L. -ltharavu_dll -o main.exe
```

---

## Configuration — tharavu.ini

```ini
[paths]
data_dir = ./data       ; root folder for logical-name resolution

[engine]
dim      = 64           ; embedding vector dimension
hash_cap = 131072       ; vocab hash table capacity (power of two)
mmap     = true         ; use memory-mapped I/O for .ovoc / .ovec

[limits]
max_word_len = 255
max_cols     = 64
max_row_size = 4096
```

---

## Files to Ship With a Consumer Project

| File | Purpose |
|---|---|
| `tharavu.dll` | Runtime — next to the consumer executable |
| `include/tharavu_dll.h` | Public header |
| `libtharavu_dll.a` | Windows import library |
| `tharavu.ini` | Edit `data_dir` per deployment |

---

## Contribution Guidelines

- Open an issue before starting any large feature.
- Keep pull requests focused — one logical change per PR.
- Include a test or sample for any new functionality.
- Maintain C99 compatibility.
- All contributions must remain GPL v3 compatible.

---

## License & Usage

This project is licensed under the **GNU General Public License v3.0 (GPL v3)** — see the `LICENSE` file.

### What this means in plain terms

| Scenario | What you can do |
|---|---|
| Use in any project | **Allowed** — commercial or non-commercial |
| Modify for internal use | **Allowed** — no obligation to publish |
| Distribute unmodified | **Allowed** — keep the GPL v3 license notice |
| Distribute modified version | **Must** release your changes under GPL v3 |
| Bundle in a proprietary product you distribute | **Must** open-source your changes under GPL v3 |

> **Copyleft in one sentence:** Anyone who receives a copy of this
> software — including your modified version — gets the same freedoms
> you received.  You cannot take those freedoms away.
