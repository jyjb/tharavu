# THARAVU — .OGPH GRAPH STORAGE DIRECTIVE
## For Claude Code — Add Native Graph File Type
## Grounded in: THARAVU.sexp (ground truth)
## Dependency: None — run first

---

## GROUND TRUTH BASELINE

From THARAVU.sexp, current state:
- Version: 1.0 / ABI v2
- File types: ODAT (TDE_FILE_ODAT=1) OVOC (TDE_FILE_OVOC=2) OVEC (TDE_FILE_OVEC=3)
- Sources: data_engine.c platform.c tharavu_dll.c
- Public header: src/include/tharavu_dll.h
- Config: .sxp loaded via tde_config_load(sxp_path) using sLispManager
- Logical name format: "dbname.tablename" → base_path/db/table.ext
- Build: cmake, shared target links sLispManager

No .OGPH exists today. This directive adds it.

---

## RULES

- Follow THARAVU's existing patterns exactly:
  binary flat files, mmap-backed, atomic write-then-rename,
  zero-alloc hot path, arena allocation for builders
- All new graph functions prefixed tgph_ (THARAVU GraPH)
- File extension .ogph, magic bytes "OGPH"
- New constant TDE_FILE_OGPH = 4
- Logical name resolution follows existing pattern:
  "dbname.graphname" → base_path/dbname/graphname.ogph
- Config stays .sxp — do not add any .ini config
- Do not modify any existing tde_* function
- sLispManager is already linked — use it if metadata
  serialization needs S-expression format

---

## FILE FORMAT — .OGPH (CSR)

```
┌─────────────────────────────────────────┐
│ HEADER (64 bytes)                       │
│   magic[4]         "OGPH"               │
│   abi_version      uint16  = 1          │
│   flags            uint16               │
│   node_count       uint32               │
│   edge_count       uint32               │
│   node_prop_dim    uint32  embed dim    │
│                    0 = no embeddings    │
│   string_pool_size uint32  bytes        │
│   reserved[40]     zeroed               │
├─────────────────────────────────────────┤
│ NODE TABLE (node_count × 32 bytes)      │
│   node_id    uint64                     │
│   type_id    uint32  OGPH_NODE_*        │
│   label_off  uint32  offset into pool   │
│   embed_off  uint32  offset into embed  │
│              0xFFFFFFFF = no embed      │
│   edge_start uint32  CSR row pointer    │
│   node_flags uint32  domain/provenance  │
├─────────────────────────────────────────┤
│ CSR ROW POINTERS                        │
│   uint32[node_count + 1]                │
│   row_ptr[i]..row_ptr[i+1] = edges of i │
│   row_ptr[node_count] = edge_count      │
├─────────────────────────────────────────┤
│ EDGE ARRAY (edge_count × 16 bytes)      │
│   target_node uint32  node table index  │
│   type_id     uint16  OGPH_EDGE_*       │
│   edge_flags  uint16                    │
│   weight      float32  0.0–1.0          │
│   reserved    uint32  zeroed            │
├─────────────────────────────────────────┤
│ STRING POOL (string_pool_size bytes)    │
│   null-terminated strings packed        │
│   node labels, concept names            │
├─────────────────────────────────────────┤
│ EMBEDDING BLOCK                         │
│   float32[node_prop_dim] per node       │
│   only nodes where embed_off != MAX     │
│   same dim as SORKUVAI/OVEC (64)        │
└─────────────────────────────────────────┘
```

---

## CONSTANTS

Add to tharavu_dll.h:

```c
/* File type — extends existing TDE_FILE_* set */
#define TDE_FILE_OGPH  4

/* Header flags */
#define OGPH_FLAG_DIRECTED  0x0001
#define OGPH_FLAG_WEIGHTED  0x0002
#define OGPH_FLAG_TYPED     0x0004
#define OGPH_FLAG_EMBEDDED  0x0008

/* Node type IDs — cognitive primitives */
#define OGPH_NODE_OBJECT        0x01
#define OGPH_NODE_ACTOR         0x02
#define OGPH_NODE_ACTION        0x03
#define OGPH_NODE_STATE         0x04
#define OGPH_NODE_GOAL          0x05
#define OGPH_NODE_DEPENDENCY    0x06
#define OGPH_NODE_CONSTRAINT    0x07
#define OGPH_NODE_RESOURCE      0x08
#define OGPH_NODE_RELATIONSHIP  0x09
#define OGPH_NODE_TRANSITION    0x0A
#define OGPH_NODE_CONTEXT       0x0B
#define OGPH_NODE_VALIDATION    0x0C
#define OGPH_NODE_FAILURE       0x0D
#define OGPH_NODE_RECOVERY      0x0E
#define OGPH_NODE_POLICY        0x0F
#define OGPH_NODE_CONTRADICTION 0x10
#define OGPH_NODE_CONCEPT       0x11  /* generic domain concept */

/* Edge type IDs */
/* Knowledge graph */
#define OGPH_EDGE_IS_A          0x0001
#define OGPH_EDGE_HAS           0x0002
#define OGPH_EDGE_REQUIRES      0x0003
#define OGPH_EDGE_PERFORMS      0x0004
#define OGPH_EDGE_PRODUCES      0x0005
#define OGPH_EDGE_RELATED_TO    0x0006
#define OGPH_EDGE_CONSTRAINS    0x0007
/* Semantic graph */
#define OGPH_EDGE_DEPENDS_ON     0x0010
#define OGPH_EDGE_TRANSITIONS_TO 0x0011
#define OGPH_EDGE_CAUSAL         0x0012
#define OGPH_EDGE_CONTRADICTS    0x0013
#define OGPH_EDGE_RECOVERS_VIA   0x0014

/* Edge flags */
#define OGPH_EDGE_FLAG_LOW_CONFIDENCE 0x0001
```

---

## NEW SOURCE FILES

### src/ogph.h (internal — not installed)

```c
#ifndef OGPH_H
#define OGPH_H

#include <stdint.h>
#include "tharavu_dll.h"

/* Opaque graph handle */
typedef struct ogph_graph_s ogph_graph_t;

/* ── Open / Close ─────────────────────────────────────── */
ogph_graph_t *tgph_open        (const char *path);
ogph_graph_t *tgph_open_logical(const char *logical_name);
int           tgph_save        (const ogph_graph_t *g, const char *path);
int           tgph_save_logical(const ogph_graph_t *g,
                                 const char *logical_name);
void          tgph_close       (ogph_graph_t *g);

/* ── Graph metadata ──────────────────────────────────── */
int      tgph_node_count      (const ogph_graph_t *g);
int      tgph_edge_count_total(const ogph_graph_t *g);
uint16_t tgph_flags           (const ogph_graph_t *g);
int      tgph_embed_dim       (const ogph_graph_t *g);

/* ── Node queries — O(1) via hash index ──────────────── */
int          tgph_find_node  (const ogph_graph_t *g, uint64_t node_id);
uint64_t     tgph_node_id    (const ogph_graph_t *g, int idx);
int          tgph_node_type  (const ogph_graph_t *g, int idx);
const char  *tgph_node_label (const ogph_graph_t *g, int idx);
uint32_t     tgph_node_flags (const ogph_graph_t *g, int idx);
const float *tgph_node_embed (const ogph_graph_t *g, int idx);

/* ── Edge traversal — CSR O(degree) ─────────────────── */
int tgph_next_edge(const ogph_graph_t *g,
                   int      node_idx,
                   int     *edge_cursor,   /* in/out; init to 0 */
                   int     *target_idx,    /* out */
                   uint16_t *type_id,      /* out */
                   float    *weight);      /* out */
int tgph_out_degree(const ogph_graph_t *g, int node_idx);

#endif /* OGPH_H */
```

### src/ogph_builder.h (internal — not installed)

```c
#ifndef OGPH_BUILDER_H
#define OGPH_BUILDER_H

#include <stdint.h>
#include "ogph.h"

typedef struct ogph_builder_s ogph_builder_t;

/* Create builder
   node_capacity / edge_capacity: hints, not hard limits
   embed_dim: 0 = no embeddings
   flags: OGPH_FLAG_* bitmask */
ogph_builder_t *tgph_builder_create(int node_capacity,
                                     int edge_capacity,
                                     int embed_dim,
                                     uint16_t flags);

/* Add node — returns node index >= 0 or -1 on error
   embed may be NULL (embed_off set to 0xFFFFFFFF) */
int tgph_builder_add_node(ogph_builder_t *b,
                           uint64_t    node_id,
                           uint32_t    type_id,
                           const char *label,
                           const float *embed,
                           uint32_t    node_flags);

/* Add directed edge — src_idx / tgt_idx from add_node */
int tgph_builder_add_edge(ogph_builder_t *b,
                           int      src_idx,
                           int      tgt_idx,
                           uint16_t type_id,
                           float    weight,
                           uint16_t edge_flags);

/* Finalise: sort into CSR order, write .ogph atomically */
int tgph_builder_finalise        (ogph_builder_t *b,
                                   const char *path);
int tgph_builder_finalise_logical(ogph_builder_t *b,
                                   const char *logical_name);

void tgph_builder_free(ogph_builder_t *b);

#endif /* OGPH_BUILDER_H */
```

---

## FILES TO MODIFY

### src/include/tharavu_dll.h

Add after existing TDE_FILE_OVEC constant:
```c
#define TDE_FILE_OGPH  4
```

Add all OGPH_FLAG_*, OGPH_NODE_*, OGPH_EDGE_* constants.

Add #include of ogph.h and ogph_builder.h so callers get the
full graph API from a single tharavu_dll.h include.

### CMakeLists.txt

Add ogph.c and ogph_builder.c to the sources list for both
the shared tharavu target and the static tharavu_static target:

```cmake
target_sources(tharavu PRIVATE
    src/data_engine.c
    src/platform.c
    src/tharavu_dll.c
    src/ogph.c           # ADD
    src/ogph_builder.c   # ADD
)
```

---

## IMPLEMENTATION NOTES

### ogph.c — memory mapping
Follow data_engine.c mmap pattern:
- mmap on POSIX, CreateFileMapping/MapViewOfFile on Windows
  (platform.c already abstracts this)
- Read-only mmap after tgph_open
- String pool and embedding block are zero-copy pointer offsets
- Hash index over node_id → node_index built at open time
  using FNV-1a (consistent with OVOC pattern in THARAVU)
- Hash table lives in heap alongside mmap, freed at tgph_close

### ogph.c — tgph_save
Follow tde_save pattern: write to temp file, rename atomically.
Use platform.c file ops for cross-platform atomic rename.

### ogph_builder.c — CSR construction
On tgph_builder_finalise():
1. Sort edges by source node index (qsort or counting sort)
2. Build row_ptr[] by counting edges per source node
3. Write header, node table, row_ptr, edge array,
   string pool, embedding block in layout order
4. Temp file + atomic rename

### Arena allocation in builder
Use same arena pattern as existing THARAVU internals for
string pool accumulation and edge list during build phase.
No heap allocation in tgph_next_edge hot path.

### Logical name resolution
tgph_open_logical / tgph_save_logical / tgph_builder_finalise_logical
resolve "db.name" → base_path/db/name.ogph
using the same base_path set by tde_set_base_path().
Same two-part "dbname.tablename" format as ODAT/OVEC/OVOC.

---

## VERIFICATION TESTS

```c
/* Test 1: Round-trip write and read */
float embed_a[64] = {0};
embed_a[0] = 1.0f; /* distinguishable test vector */

ogph_builder_t *b = tgph_builder_create(4, 4, 64,
    OGPH_FLAG_DIRECTED | OGPH_FLAG_TYPED |
    OGPH_FLAG_WEIGHTED | OGPH_FLAG_EMBEDDED);

int idx_a = tgph_builder_add_node(b, 1001,
    OGPH_NODE_OBJECT, "Invoice", embed_a, 0);
int idx_b = tgph_builder_add_node(b, 1002,
    OGPH_NODE_ACTOR,  "Vendor",  NULL, 0);
tgph_builder_add_edge(b, idx_a, idx_b,
    OGPH_EDGE_RELATED_TO, 0.85f, 0);
tgph_builder_finalise(b, "test.ogph");
tgph_builder_free(b);

ogph_graph_t *g = tgph_open("test.ogph");
assert(g != NULL);
```

- [ ] `tgph_node_count(g) == 2`
- [ ] `tgph_edge_count_total(g) == 1`
- [ ] `tgph_find_node(g, 1001) >= 0` — O(1) hash lookup
- [ ] `tgph_find_node(g, 9999) == -1` — not found
- [ ] `tgph_node_label(g, tgph_find_node(g, 1001))` == "Invoice"
- [ ] `tgph_node_type(g, tgph_find_node(g, 1001))` == OGPH_NODE_OBJECT
- [ ] `tgph_node_embed(g, tgph_find_node(g, 1001))[0]` ≈ 1.0f
- [ ] `tgph_node_embed(g, tgph_find_node(g, 1002))` == NULL (no embed)
- [ ] Edge traversal from node 1001: target=node 1002 index,
      type=OGPH_EDGE_RELATED_TO, weight≈0.85f
- [ ] `tgph_file_type` of opened handle == TDE_FILE_OGPH

```c
/* Test 2: Logical name resolution */
tde_set_base_path("./data/knowledge");
tgph_save_logical(g, "semantic.test");
tgph_close(g);
```
- [ ] File created at `./data/knowledge/semantic/test.ogph`
- [ ] File size > 64 bytes (header present)
- [ ] Timestamp is today

```c
/* Test 3: Performance */
/* Build graph: 1000 nodes, 5000 edges */
/* tgph_find_node() on 100 random IDs */
/* Average lookup must be < 5us */
```
- [ ] O(1) node lookup confirmed under load

```c
/* Test 4: Existing THARAVU API unaffected */
tde_config_load("data/tharavu.sxp");
tde_handle_t h = tde_open_odat("knowledge.general");
assert(h != NULL);
tde_close(h);
```
- [ ] Existing tde_* functions unaffected by new ogph.c

---

## MANDATORY COMPLETION STEPS

### A — Append to CHANGELOG.log

```
=====================================
DATE: <today>
DIRECTIVE: THARAVU .OGPH Graph Storage
STATUS: COMPLETE
REPO: Z:\._repos\Tharavu
FILES CHANGED:
  src/include/tharavu_dll.h  UPDATED — TDE_FILE_OGPH=4 + constants
  src/ogph.h                 NEW
  src/ogph.c                 NEW
  src/ogph_builder.h         NEW
  src/ogph_builder.c         NEW
  CMakeLists.txt             UPDATED — added ogph sources
VERSION: 1.0 → 1.1 (ABI v2 unchanged)
NEW CAPABILITY: .OGPH CSR graph file type
  tgph_open / tgph_save / tgph_close
  tgph_find_node O(1) FNV-1a hash
  tgph_next_edge CSR O(degree)
  tgph_builder_create/add_node/add_edge/finalise
  logical name resolution: dbname.graphname → .ogph
DLL COPIED TO:
  Z:\._repos\Thiraviyan\bin\Debug\net10.0-windows\tharavu.dll
=====================================
```

### B — Update / Create THARAVU.sexp

Append to the existing (file-formats) section:
```lisp
(ogph "CSR graph storage"
  extension ".ogph"
  magic "OGPH"
  abi-version 1
  (layout header/node-table/csr-row-ptrs/edge-array/string-pool/embed-block)
  (node-lookup "FNV-1a hash index O(1)")
  (edge-traversal "CSR O(degree)")
  (node-types OBJECT ACTOR ACTION STATE GOAL DEPENDENCY CONSTRAINT
              RESOURCE RELATIONSHIP TRANSITION CONTEXT VALIDATION
              FAILURE RECOVERY POLICY CONTRADICTION CONCEPT)
  (edge-types IS_A HAS REQUIRES PERFORMS PRODUCES RELATED_TO
              CONSTRAINS DEPENDS_ON TRANSITIONS_TO CAUSAL
              CONTRADICTS RECOVERS_VIA))
```

Update (capabilities) to add:
```lisp
(graph-store ".OGPH: CSR graph with O(1) node lookup and O(degree) edge traversal")
```

Update version to "1.1".

---

## CHECKLIST

- [ ] ogph.h and ogph.c created
- [ ] ogph_builder.h and ogph_builder.c created
- [ ] TDE_FILE_OGPH = 4 added to tharavu_dll.h
- [ ] All OGPH_* constants added to tharavu_dll.h
- [ ] ogph.h and ogph_builder.h included from tharavu_dll.h
- [ ] CMakeLists.txt updated for both targets
- [ ] tharavu.dll builds clean zero errors
- [ ] All 4 verification tests pass
- [ ] Existing tde_* tests unaffected
- [ ] tharavu.dll copied to Thiraviyan output — timestamp today
- [ ] CHANGELOG.log updated
- [ ] THARAVU.sexp updated
