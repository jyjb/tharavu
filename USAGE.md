# Tharavu Data Engine — Usage Guide

Tharavu is a shared library (DLL on Windows, SO on Linux/macOS).
This guide shows how to call it from **C, C++, C#, F#, Python, and Java**.

All exported functions use the `__cdecl` calling convention on Windows
and the platform default on POSIX.  Include only `tharavu_dll.h`.

---

## Prerequisites

Place these files next to your executable (or on the system PATH):

```
tharavu.dll   (Windows) — or libtharavu.so (Linux/macOS)
tharavu.ini   (edit data_dir for your deployment)
```

---

## Core Concepts

### Logical Names

Instead of raw file paths, Tharavu uses logical names of the form
`"dbname.tablename"`.  These resolve to:

```
{data_dir}/dbname/tablename.odat   — tabular data
{data_dir}/dbname/tablename.ovoc   — vocabulary hash table
{data_dir}/dbname/tablename.ovec   — embedding vectors
```

`data_dir` is set by `tharavu.ini`.  Missing directories are created
automatically.

### Handle Lifecycle

```
tde_open_*(logical_name)  or  tde_create(cols, n)
    ↓  tde_row_*, tde_get_*, tde_find, tde_vector_*
tde_close(h)
```

Always call `tde_close` — even on error paths.

---

## C

### ODAT — Create, insert, save, reload, query

```c
#include "tharavu_dll.h"
#include <stdio.h>

int main(void) {
    tde_config_load("tharavu.ini");

    /* Create an in-memory table with three columns */
    const char *cols[] = {"id", "label", "score"};
    tde_handle_t t = tde_create(cols, 3);

    /* Insert three rows */
    static const char *labels[] = {"alpha", "beta", "gamma"};
    for (int i = 0; i < 3; i++) {
        tde_handle_t row = tde_row_begin(t);
        tde_row_set_int32  (row, 0, i);
        tde_row_set_string (row, 1, labels[i]);
        tde_row_set_float32(row, 2, (float)i * 0.5f);
        tde_row_commit(row);
    }

    /* Save, then reload */
    tde_save_logical(t, "demo.words");
    tde_close(t);

    t = tde_open_odat("demo.words");
    printf("rows: %d\n", tde_row_count(t));   /* 3 */

    /* Query: find all rows where label == "beta" */
    tde_handle_t result = tde_find(t, "label", "beta");
    if (result) {
        char buf[64] = {0};
        tde_get_string(result, 0, 1, buf, sizeof(buf));
        printf("found: %s\n", buf);            /* beta */
        tde_close(result);
    }

    /* Zero-alloc index query */
    int idx[10];
    int n = tde_find_ids(t, "label", "gamma", idx, 10);
    printf("gamma at row index %d\n", idx[0]);  /* 2 */

    tde_close(t);
    return 0;
}
```

### OVEC — Build and top-k vector search

```c
#include "tharavu_dll.h"
#include <stdio.h>

int main(void) {
    tde_config_load("tharavu.ini");

    /* Build 5 orthogonal unit vectors of dimension 8 */
    float data[5 * 8] = {0};
    for (int i = 0; i < 5; i++) data[i * 8 + i] = 1.0f;
    tde_build_vectors_logical("demo.vecs", data, 5, 8);

    /* Search: query = unit vector in dimension 3 */
    tde_handle_t h = tde_open_ovec("demo.vecs");

    float query[8] = {0};
    query[3] = 1.0f;

    uint32_t ids[3];
    float    scores[3];
    int found = tde_vector_search_topk(h, query, 8, 3, ids, scores);
    for (int i = 0; i < found; i++)
        printf("rank %d — row=%u  score=%.4f\n", i+1, ids[i], scores[i]);
    /* rank 1 — row=3  score=1.0000  (exact match) */

    tde_close(h);
    return 0;
}
```

### OVOC — Build and lookup

```c
#include "tharavu_dll.h"
#include <stdio.h>

int main(void) {
    tde_config_load("tharavu.ini");

    /* Build vocabulary: token ID == array index */
    const char *words[] = {"one", "two", "three", "four", "five"};
    tde_build_vocab_logical("demo.vocab", words, 5);

    tde_handle_t h = tde_open_ovoc("demo.vocab");

    /* Forward lookup */
    uint32_t id = 0;
    tde_vocab_lookup(h, "three", &id);
    printf("three => %u\n", id);   /* 2 */

    /* Reverse lookup — pointer directly into mmap, NOT NUL-terminated */
    uint16_t len = 0;
    const char *w = tde_vocab_reverse_lookup(h, 1, &len);
    printf("id 1 => %.*s\n", (int)len, w);   /* two */
    /* Never use: printf("%s", w)  — the pointer has no NUL terminator */

    tde_close(h);
    return 0;
}
```

**Build:**
```bash
gcc main.c -std=c99 -I include -L. -ltharavu_dll -o main.exe
```

---

## C++

```cpp
#include "tharavu_dll.h"
#include <iostream>
#include <vector>
#include <stdexcept>

struct Handle {
    tde_handle_t h;
    explicit Handle(tde_handle_t h_) : h(h_) {
        if (!h) throw std::runtime_error(tde_strerror(tde_last_error()));
    }
    ~Handle() { tde_close(h); }
    operator tde_handle_t() const { return h; }
};

int main() {
    tde_config_load("tharavu.ini");

    /* Build 4-dimensional unit vectors */
    std::vector<float> data(4 * 4, 0.0f);
    for (int i = 0; i < 4; i++) data[(size_t)i * 4 + i] = 1.0f;
    tde_build_vectors_logical("cxx.vecs", data.data(), 4, 4);

    Handle h(tde_open_ovec("cxx.vecs"));

    float query[4] = {0, 0, 1, 0};
    std::vector<uint32_t> ids(4);
    std::vector<float>    scores(4);
    int n = tde_vector_search_topk(h, query, 4, 4,
                                    ids.data(), scores.data());
    for (int i = 0; i < n; i++)
        std::cout << "rank " << i+1
                  << "  row=" << ids[i]
                  << "  score=" << scores[i] << "\n";
    return 0;
}
```

**Build:**
```bash
g++ main.cpp -std=c++17 -I include -L. -ltharavu_dll -o main.exe
```

---

## C# (.NET / P/Invoke)

```csharp
using System;
using System.Runtime.InteropServices;

static class Tharavu
{
    const string Dll  = "tharavu.dll";
    const CallingConvention Cdecl = CallingConvention.Cdecl;

    [DllImport(Dll, CallingConvention = Cdecl)]
    public static extern int tde_config_load(
        [MarshalAs(UnmanagedType.LPStr)] string ini);

    [DllImport(Dll, CallingConvention = Cdecl)]
    public static extern IntPtr tde_open_ovec(
        [MarshalAs(UnmanagedType.LPStr)] string name);

    [DllImport(Dll, CallingConvention = Cdecl)]
    public static extern void tde_close(IntPtr h);

    [DllImport(Dll, CallingConvention = Cdecl)]
    public static extern int tde_build_vectors_logical(
        [MarshalAs(UnmanagedType.LPStr)] string name,
        [In] float[] data, int count, uint dim);

    [DllImport(Dll, CallingConvention = Cdecl)]
    public static extern int tde_vector_search_topk(
        IntPtr h, [In] float[] query, uint dim, uint k,
        [Out] uint[] ids, [Out] float[] scores);

    [DllImport(Dll, CallingConvention = Cdecl)]
    [return: MarshalAs(UnmanagedType.LPStr)]
    public static extern string tde_strerror(int code);
}

class Program {
    static void Main() {
        Tharavu.tde_config_load("tharavu.ini");

        float[] data = new float[4 * 4];
        for (int i = 0; i < 4; i++) data[i * 4 + i] = 1.0f;
        Tharavu.tde_build_vectors_logical("cs.vecs", data, 4, 4u);

        IntPtr  h      = Tharavu.tde_open_ovec("cs.vecs");
        float[] query  = {0, 0, 1, 0};
        uint[]  ids    = new uint[4];
        float[] scores = new float[4];
        int n = Tharavu.tde_vector_search_topk(h, query, 4u, 4u, ids, scores);
        for (int i = 0; i < n; i++)
            Console.WriteLine($"rank {i+1}  row={ids[i]}  score={scores[i]:F4}");
        Tharavu.tde_close(h);
    }
}
```

---

## F#

```fsharp
open System.Runtime.InteropServices

[<Literal>]
let Dll = "tharavu.dll"

[<DllImport(Dll, CallingConvention = CallingConvention.Cdecl)>]
extern int tde_config_load(string ini)

[<DllImport(Dll, CallingConvention = CallingConvention.Cdecl)>]
extern nativeint tde_open_ovec(string name)

[<DllImport(Dll, CallingConvention = CallingConvention.Cdecl)>]
extern void tde_close(nativeint h)

[<DllImport(Dll, CallingConvention = CallingConvention.Cdecl)>]
extern int tde_build_vectors_logical(string name,
                                      float32[] data, int n, uint32 dim)

[<DllImport(Dll, CallingConvention = CallingConvention.Cdecl)>]
extern int tde_vector_search_topk(nativeint h, float32[] query,
                                   uint32 dim, uint32 k,
                                   [<Out>] uint32[]  ids,
                                   [<Out>] float32[] scores)

[<EntryPoint>]
let main _ =
    tde_config_load("tharavu.ini") |> ignore
    let data = Array.zeroCreate<float32> (4 * 4)
    for i in 0..3 do data.[i * 4 + i] <- 1.0f
    tde_build_vectors_logical("fs.vecs", data, 4, 4u) |> ignore
    let h      = tde_open_ovec("fs.vecs")
    let query  = [| 0.0f; 0.0f; 1.0f; 0.0f |]
    let ids    = Array.zeroCreate<uint32>  4
    let scores = Array.zeroCreate<float32> 4
    let n = tde_vector_search_topk(h, query, 4u, 4u, ids, scores)
    for i in 0..n-1 do
        printfn "rank %d  row=%d  score=%.4f" (i+1) ids.[i] scores.[i]
    tde_close(h)
    0
```

---

## Python (ctypes)

```python
import ctypes, sys

_lib = ctypes.CDLL(
    "tharavu.dll" if sys.platform == "win32" else "./libtharavu.so"
)

_lib.tde_config_load.argtypes           = [ctypes.c_char_p]
_lib.tde_config_load.restype            = ctypes.c_int
_lib.tde_open_ovec.argtypes             = [ctypes.c_char_p]
_lib.tde_open_ovec.restype              = ctypes.c_void_p
_lib.tde_close.argtypes                 = [ctypes.c_void_p]
_lib.tde_close.restype                  = None
_lib.tde_build_vectors_logical.argtypes = [
    ctypes.c_char_p,
    ctypes.POINTER(ctypes.c_float),
    ctypes.c_int,
    ctypes.c_uint32,
]
_lib.tde_build_vectors_logical.restype  = ctypes.c_int
_lib.tde_vector_search_topk.argtypes    = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_float),
    ctypes.c_uint32, ctypes.c_uint32,
    ctypes.POINTER(ctypes.c_uint32),
    ctypes.POINTER(ctypes.c_float),
]
_lib.tde_vector_search_topk.restype     = ctypes.c_int

_lib.tde_config_load(b"tharavu.ini")

dim, n = 4, 4
data = (ctypes.c_float * (n * dim))()
for i in range(n):
    data[i * dim + i] = 1.0

_lib.tde_build_vectors_logical(b"py.vecs", data, n, dim)

h      = _lib.tde_open_ovec(b"py.vecs")
query  = (ctypes.c_float * dim)(0, 0, 1, 0)
ids    = (ctypes.c_uint32 * n)()
scores = (ctypes.c_float  * n)()

found = _lib.tde_vector_search_topk(h, query, dim, n, ids, scores)
for i in range(found):
    print(f"rank {i+1}  row={ids[i]}  score={scores[i]:.4f}")

_lib.tde_close(h)
```

---

## Java (JNA)

```java
import com.sun.jna.*;

public class TharavuDemo {

    public interface TharavuLib extends Library {
        TharavuLib INSTANCE = Native.load("tharavu", TharavuLib.class);

        int     tde_config_load(String ini);
        Pointer tde_open_ovec(String name);
        void    tde_close(Pointer h);
        int     tde_build_vectors_logical(String name,
                                           float[] data, int n, int dim);
        int     tde_vector_search_topk(Pointer h, float[] query,
                                        int dim, int k,
                                        int[] ids, float[] scores);
        String  tde_strerror(int code);
    }

    public static void main(String[] args) {
        TharavuLib tde = TharavuLib.INSTANCE;
        tde.tde_config_load("tharavu.ini");

        int dim = 4, n = 4;
        float[] data = new float[n * dim];
        for (int i = 0; i < n; i++) data[i * dim + i] = 1.0f;
        tde.tde_build_vectors_logical("java.vecs", data, n, dim);

        Pointer h      = tde.tde_open_ovec("java.vecs");
        float[] query  = {0, 0, 1, 0};
        int[]   ids    = new int[n];
        float[] scores = new float[n];
        int found = tde.tde_vector_search_topk(h, query, dim, n, ids, scores);
        for (int i = 0; i < found; i++)
            System.out.printf("rank %d  row=%d  score=%.4f%n",
                               i+1, ids[i], scores[i]);
        tde.tde_close(h);
    }
}
```

**Compile and run:**
```bash
javac -cp jna-5.14.0.jar TharavuDemo.java
java  -cp ".;jna-5.14.0.jar" TharavuDemo
```

---

## Two-Call String Pattern

Any function that returns a string uses this pattern to avoid fixed
buffer sizes:

```c
/* Step 1 — query required size (pass NULL buffer) */
int size = tde_get_string(h, row, col, NULL, 0);

/* Step 2 — allocate and retrieve */
char *buf = malloc(size);
tde_get_string(h, row, col, buf, size);
printf("%s\n", buf);
free(buf);
```

---

## Zero-Copy Vocab Reverse Lookup

`tde_vocab_reverse_lookup` returns a pointer **directly into the mmap
region** — it has **no NUL terminator**.  Always use the `out_len`
parameter:

```c
uint16_t len = 0;
const char *w = tde_vocab_reverse_lookup(h, token_id, &len);
if (w)
    printf("%.*s\n", (int)len, w);   /* correct */
/* Never:  printf("%s\n", w);        — undefined behavior */
```

---

## Error Handling

```c
tde_handle_t h = tde_open_odat("demo.words");
if (!h) {
    fprintf(stderr, "open failed: %s\n", tde_strerror(tde_last_error()));
    return 1;
}
```

`tde_last_error()` is thread-local — each thread tracks its own state.
