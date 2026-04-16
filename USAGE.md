# Usage Guide

Tharavu Data Engine is a hybrid project: it provides a shared library and a set of CLI utilities for working with portable data formats.

## 1. Initialization

Before using the library or the sample command-line tools, make sure the configuration file exists or can be created:

```c
tde_config_load("tharavu.ini");
```

On first use, the library can create a default `tharavu.ini` and the configured base data directory if they do not already exist.

## 2. Command-Line Tools

The repository includes three main CLI helpers:

- `de_dump` — dump contents of `.odat`, `.ovoc`, and `.ovec` files
- `de_import` — import CSV data into an `.odat` file
- `de_crud` — perform basic CRUD operations on `.odat` files

### de_dump

Usage:

```bash
de_dump <file>
```

Example:

```bash
de_dump sample/data/demo/users.odat
```

Behavior:

- Prints metadata and contents for supported files.
- Detects file format from header.
- Includes error output for unreadable or corrupted files.

### de_import

Usage:

```bash
de_import <input.csv> <output.odat>
```

Example:

```bash
de_import sample/data/users.csv sample/data/demo/users.odat
```

Behavior:

- Reads a CSV file and converts rows into ODAT rows.
- Writes a new `.odat` table at the destination path.
- Produces a clear error if the input file is missing or invalid.

### de_crud

Usage:

```bash
de_crud <table.odat> <command> [args]
```

Supported commands:

- `list` — list rows and metadata
- `read <row>` — read a specific row
- `insert "id=5,name=Bob"` — insert a new row
- `update <row> <col> <value>` — update a cell
- `delete <row>` — delete a row

Example:

```bash
de_crud sample/data/demo/users.odat list
```

## 3. Library Usage

### C (Logical Names)

```c
#include "tharavu_dll.h"
#include <stdio.h>

int main(void) {
    tde_config_load("tharavu.ini");  // Sets base path to ./data
    const char *cols[] = {"id", "name"};
    tde_handle_t table = tde_create(cols, 2);
    tde_handle_t row = tde_row_begin(table);
    tde_row_set_int32(row, 0, 1);
    tde_row_set_string(row, 1, "Alice");
    tde_row_commit(row);
    tde_save_logical(table, "demo.users");  // Creates ./data/demo/users.odat
    tde_close(table);
    
    // Later load it back
    tde_handle_t loaded = tde_open_odat("demo.users");  // Loads ./data/demo/users.odat
    tde_close(loaded);
    return 0;
}
```

### C++

```cpp
extern "C" {
#include "tharavu_dll.h"
}
#include <iostream>

int main() {
    tde_config_load("tharavu.ini");
    const char *cols[] = {"id", "name"};
    tde_handle_t table = tde_create(cols, 2);
    tde_handle_t row = tde_row_begin(table);
    tde_row_set_int32(row, 0, 1);
    tde_row_set_string(row, 1, "Alice");
    tde_row_commit(row);
    tde_save(table, "./data/demo/users.odat");
    tde_close(table);
    std::cout << "Saved sample table" << std::endl;
    return 0;
}
```

### C#

```csharp
using System;
using System.Runtime.InteropServices;

class Program {
    const string DLL = "tharavu.dll";

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int tde_config_load(string iniPath);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr tde_create(string[] colNames, int colCount);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern int tde_save_logical(IntPtr h, string logicalName);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern IntPtr tde_open_odat(string logicalName);

    [DllImport(DLL, CallingConvention = CallingConvention.Cdecl)]
    public static extern void tde_close(IntPtr h);

    static void Main() {
        tde_config_load("tharavu.ini");
        var cols = new[] { "id", "name" };
        IntPtr table = tde_create(cols, cols.Length);
        // ... populate table ...
        tde_save_logical(table, "demo.users");  // Creates ./data/demo/users.odat
        tde_close(table);
        
        // Later load it back
        IntPtr loaded = tde_open_odat("demo.users");  // Loads ./data/demo/users.odat
        tde_close(loaded);
    }
}
```

### F#

```fsharp
open System
open System.Runtime.InteropServices

module Tharavu =
    [<DllImport("tharavu.dll", CallingConvention = CallingConvention.Cdecl)>]
    extern int tde_config_load(string iniPath)

    [<DllImport("tharavu.dll", CallingConvention = CallingConvention.Cdecl)>]
    extern IntPtr tde_create(string[] colNames, int colCount)

    [<DllImport("tharavu.dll", CallingConvention = CallingConvention.Cdecl)>]
    extern int tde_save_logical(IntPtr h, string logicalName)

    [<DllImport("tharavu.dll", CallingConvention = CallingConvention.Cdecl)>]
    extern IntPtr tde_open_odat(string logicalName)

    [<DllImport("tharavu.dll", CallingConvention = CallingConvention.Cdecl)>]
    extern void tde_close(IntPtr h)

[<EntryPoint>]
let main argv =
    Tharavu.tde_config_load("tharavu.ini") |> ignore
    let cols = [| "id"; "name" |]
    let table = Tharavu.tde_create(cols, cols.Length)
    // ... populate table ...
    Tharavu.tde_save_logical(table, "demo.users") |> ignore  // Creates ./data/demo/users.odat
    Tharavu.tde_close(table)
    
    // Later load it back
    let loaded = Tharavu.tde_open_odat("demo.users")  // Loads ./data/demo/users.odat
    Tharavu.tde_close(loaded)
    0
```

### Python

```python
import ctypes

lib = ctypes.CDLL("tharavu.dll")
lib.tde_config_load.argtypes = [ctypes.c_char_p]
lib.tde_config_load.restype = ctypes.c_int
lib.tde_config_load(b"tharavu.ini")

lib.tde_create.argtypes = [ctypes.POINTER(ctypes.c_char_p), ctypes.c_int]
lib.tde_create.restype = ctypes.c_void_p
cols = (ctypes.c_char_p * 2)(b"id", b"name")
handle = lib.tde_create(cols, 2)

lib.tde_save_logical.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
lib.tde_save_logical.restype = ctypes.c_int
lib.tde_save_logical(handle, b"demo.users")  # Creates ./data/demo/users.odat

lib.tde_open_odat.argtypes = [ctypes.c_char_p]
lib.tde_open_odat.restype = ctypes.c_void_p
loaded = lib.tde_open_odat(b"demo.users")  # Loads ./data/demo/users.odat

lib.tde_close(handle)
lib.tde_close(loaded)
```

### Java

```java
import com.sun.jna.*;

public interface TharavuLib extends Library {
    TharavuLib INSTANCE = Native.load("tharavu", TharavuLib.class);
    int tde_config_load(String iniPath);
    Pointer tde_create(String[] colNames, int colCount);
    int tde_save_logical(Pointer h, String logicalName);
    Pointer tde_open_odat(String logicalName);
    void tde_close(Pointer h);
}

public class Example {
    public static void main(String[] args) {
        TharavuLib lib = TharavuLib.INSTANCE;
        lib.tde_config_load("tharavu.ini");
        Pointer handle = lib.tde_create(new String[]{"id", "name"}, 2);
        // ... populate table ...
        lib.tde_save_logical(handle, "demo.users");  // Creates ./data/demo/users.odat
        lib.tde_close(handle);
        
        // Later load it back
        Pointer loaded = lib.tde_open_odat("demo.users");  // Loads ./data/demo/users.odat
        lib.tde_close(loaded);
    }
}
```

## 4. Exported functions

The library exposes a set of `tde_*` functions for:

- configuration setup
- opening and saving ODAT/OVOC/OVEC files
- row creation and querying
- vocabulary lookup
- embedding vector access
- error reporting

## 5. Practical advice

- Call `tde_config_load("tharavu.ini")` before logical path operations.
- Use logical names like `"demo.users"` instead of file paths like `"./data/demo/users.odat"`.
- Keep the shared library next to your executable for .NET and Java bindings.
- Always close handles with `tde_close()`.
- Use `tde_get_string()` in a two-step read for safe buffer allocation.
- When distributing modified builds, keep the result under GPL v3.
