(module THARAVU
  (version "1.2")
  (abi-version 2)
  (language C99)
  (build-system cmake
    (min-version "3.15")
    (project-version "1.0")
    (targets
      (shared tharavu
        (sources data_engine.c platform.c tharavu_dll.c ogph.c ogph_builder.c)
        (public-header src/include/tharavu_dll.h)
        (defines THARAVU_EXPORTS)
        (links sLispManager))
      (static tharavu_static
        (sources data_engine.c platform.c ogph.c ogph_builder.c)
        (links sLispManager))
      (cli-tools de_dump de_import de_crud
        (condition "EXISTS src/tools/<tool>.c")
        (links tharavu_static))
      (sample create_sample_db
        (condition "EXISTS docs/sample/create_sample_db.c")
        (links tharavu_static))))
  (dependencies
    (external sLispManager
      (path "${CMAKE_SOURCE_DIR}/../sLispManager")
      (lib "build/libslispmanager.dll.a")
      (purpose "SXP config file parsing")))
  (file-formats
    (odat "tabular row/column data" extension ".odat" magic-detected true)
    (ovoc "vocabulary hash table" extension ".ovoc" magic-detected true)
    (ovec "embedding vectors" extension ".ovec" magic-detected true)
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
                  CONTRADICTS RECOVERS_VIA)))
  (public-api
    (header "src/include/tharavu_dll.h")
    (handle-type "tde_handle_t" opaque true underlying "void *")
    (calling-convention
      (windows "__cdecl" macro "THARAVU_CALL")
      (posix "default" macro "THARAVU_CALL"))
    (constants
      (TDE_TYPE_NULL    0)
      (TDE_TYPE_INT32   1)
      (TDE_TYPE_INT64   2)
      (TDE_TYPE_FLOAT   3)
      (TDE_TYPE_STRING  4)
      (TDE_TYPE_BLOB    5)
      (TDE_FILE_ODAT    1)
      (TDE_FILE_OVOC    2)
      (TDE_FILE_OVEC    3)
      (TDE_FILE_OGPH    4)
      (TDE_OK           0)
      (TDE_ERR_IO      -1)
      (TDE_ERR_CORRUPT -2)
      (TDE_ERR_MEM     -3)
      (TDE_ERR_LOCK    -4)
      (TDE_ERR_NOTFOUND -5)
      (TDE_ERR_INVAL   -6)
      (TDE_TOKEN_UNKNOWN UINT32_MAX)
      (THARAVU_ABI_VERSION 2))
    (functions
      (version
        (tde_version_major () -> int)
        (tde_version_minor () -> int)
        (tde_abi_version   () -> int))
      (configuration
        (tde_set_base_path (path:const-char*) -> void
          (note "default is ./data; call before any tde_open_* or tde_build_* functions"))
        (tde_get_base_path () -> const-char*
          (note "pointer to internal storage; valid until next tde_set_base_path() call"))
        (tde_set_dim (dim:uint32_t) -> void
          (note "must call before any OVEC or OVOC operations; default 64"))
        (tde_set_hash_cap (cap:uint32_t) -> void
          (note "must call before any OVOC operations; default 131072"))
        ; tde_config_load() REMOVED 2026-05-29 — config reading moved to caller via SLispManager + slm_load()
        ; Migration: slm_load(ocfg_path) -> slm_find(cfg,"tharavu") -> tde_set_base_path/tde_set_dim/tde_set_hash_cap
        )
      (table-lifecycle
        (tde_open         (filepath:const-char*) -> tde_handle_t
          (note "auto-detects file type from magic bytes"))
        (tde_open_odat    (logical_name:const-char*) -> tde_handle_t)
        (tde_open_ovoc    (logical_name:const-char*) -> tde_handle_t)
        (tde_open_ovec    (logical_name:const-char*) -> tde_handle_t)
        (tde_create       (col_names:const-char** col_count:int) -> tde_handle_t
          (note "creates empty ODAT in memory only"))
        (tde_save         (h:tde_handle_t filepath:const-char*) -> int
          (note "atomic write-then-rename"))
        (tde_save_logical (h:tde_handle_t logical_name:const-char*) -> int)
        (tde_close        (h:tde_handle_t) -> void))
      (schema-metadata
        (tde_row_count   (h:tde_handle_t) -> int)
        (tde_col_count   (h:tde_handle_t) -> int)
        (tde_file_type   (h:tde_handle_t) -> int
          (note "returns TDE_FILE_*"))
        (tde_col_name    (h:tde_handle_t col:int) -> const-char*
          (note "pointer valid until tde_close")))
      (cell-read
        (tde_get_cell_type (h:tde_handle_t row:int col:int) -> int
          (note "returns TDE_TYPE_* or TDE_ERR_INVAL"))
        (tde_get_int32    (h:tde_handle_t row:int col:int out:int32_t*) -> int)
        (tde_get_float32  (h:tde_handle_t row:int col:int out:float*) -> int)
        (tde_get_string   (h:tde_handle_t row:int col:int buf:char* buf_size:int) -> int
          (note "two-call pattern: buf=NULL returns required size")))
      (row-builder
        (tde_row_begin      (table_h:tde_handle_t) -> tde_handle_t
          (note "returns row handle"))
        (tde_row_set_int32  (row_h:tde_handle_t col:int val:int32_t) -> int)
        (tde_row_set_float32(row_h:tde_handle_t col:int val:float) -> int)
        (tde_row_set_string (row_h:tde_handle_t col:int val:const-char*) -> int)
        (tde_row_commit     (row_h:tde_handle_t) -> int
          (note "invalidates row_h"))
        (tde_row_discard    (row_h:tde_handle_t) -> void
          (note "cancels pending row; invalidates row_h")))
      (cell-update-delete
        (tde_update_int32   (h:tde_handle_t row:int col:int val:int32_t) -> int)
        (tde_update_float32 (h:tde_handle_t row:int col:int val:float) -> int)
        (tde_update_string  (h:tde_handle_t row:int col:int val:const-char*) -> int)
        (tde_delete_row     (h:tde_handle_t row:int) -> int))
      (query
        (tde_find     (h:tde_handle_t column:const-char* value:const-char*) -> tde_handle_t
          (note "returns new independent table; caller must tde_close"))
        (tde_find_ids (h:tde_handle_t column:const-char* value:const-char*
                       ids_out:int* max_results:int) -> int
          (note "zero-alloc; returns total matches found (may exceed max_results)")))
      (vocabulary-ovoc
        (tde_vocab_lookup       (h:tde_handle_t word:const-char* out_id:uint32_t*) -> int
          (note "O(1)"))
        (tde_vocab_lookup_batch (h:tde_handle_t words:const-char** count:int
                                  tokens_out:uint32_t*) -> int
          (note "no allocation"))
        (tde_vocab_reverse_lookup    (h:tde_handle_t token_id:uint32_t
                                       out_len:uint16_t*) -> const-char*
          (note "O(1) zero-copy mmap pointer; valid until tde_close"))
        (tde_vocab_reverse_lookup_ex (h:tde_handle_t token_id:uint32_t
                                       out_len:uint16_t* out_flags:uint64_t*
                                       out_vec:const-float**) -> const-char*
          (note "also returns 64-bit domain+intent bitmask and embedded float vector"))
        (tde_vocab_reverse_batch (h:tde_handle_t token_ids:const-uint32_t*
                                   count:int words_out:const-char**) -> int
          (note "zero-copy")))
      (vectors-ovec
        (tde_vector_get_row   (h:tde_handle_t row_id:uint32_t buf:float*
                                buf_floats:uint32_t out_dim:uint32_t*) -> int
          (note "copy; returns TDE_ERR_MEM if buf_floats < dim"))
        (tde_vector_get_ptr   (h:tde_handle_t row_id:uint32_t out_dim:uint32_t*)
                                -> const-float*
          (note "zero-copy mmap pointer; valid until tde_close"))
        (tde_vector_get_batch (h:tde_handle_t row_ids:const-uint32_t* count:int
                                out_buf:float* out_dim:uint32_t*) -> int
          (note "missing IDs filled with 0.0f"))
        (tde_vector_search_topk (h:tde_handle_t query_vec:const-float* dim:uint32_t
                                  k:uint32_t ids_out:uint32_t* scores_out:float*) -> int
          (note "brute-force cosine similarity; results sorted best-first")))
      (builders
        (tde_build_vocab         (filepath:const-char* words:const-char** count:int
                                   vectors:const-float* dim:uint32_t flags:const-uint64_t*)
                                  -> int
          (note "flags=NULL means all flags=0; ABI v2 writes 8-byte flags per record"))
        (tde_build_vocab_logical (logical_name:const-char* words:const-char** count:int
                                   vectors:const-float* dim:uint32_t flags:const-uint64_t*)
                                  -> int)
        (tde_build_vectors       (filepath:const-char* data:const-float*
                                   count:int dim:uint32_t) -> int
          (note "flat row-major float array"))
        (tde_build_vectors_logical (logical_name:const-char* data:const-float*
                                     count:int dim:uint32_t) -> int))
      (export
        (tde_export_csv (h:tde_handle_t csv_filepath:const-char*) -> int
          (note "writes column headers in first row")))
      (error-handling
        (tde_strerror  (code:int) -> const-char*)
        (tde_last_error () -> int
          (note "thread-local; reset to TDE_OK at start of every API call")))))
  (internal-sources
    (data_engine.c "mmap-based table I/O; ODAT/OVOC/OVEC read-write; de_* symbols")
    (platform.c    "OS abstraction: file locking, path ops")
    (tharavu_dll.c "tde_* shim layer over de_* internals")
    (ogph.c        "OGPH graph reader: mmap open, FNV-1a hash index, CSR edge traversal; tgph_* symbols")
    (ogph_builder.c "OGPH graph builder: arena node/edge accumulation, CSR construction, atomic write"))
  (internal-header "src/include/data_engine.h"
    (note "never installed; defines table_t, cell_t, tharavuConfig, de_* functions")
    (struct table_t
      (columns col_count rows row_count row_cap mmap_ptr mmap_size
       fd is_mmap file_type filepath max_row_stride)))
  (logical-name-resolution
    (format "dbname.tablename")
    (default-base-path "./data")
    (config-override-via tde_set_base_path note "tde_config_load REMOVED 2026-05-29; caller must set base path explicitly"))
  (graph-api
    (note "OGPH file format: graph open/save/close and node/edge traversal via src/include/ogph.h; graph builder via src/include/ogph_builder.h")
    (handle-types
      (tgph_handle_t  "opaque read-only graph handle")
      (tgph_builder_t "opaque mutable graph builder"))
    (constants
      (OGPH_FLAG_DIRECTED  0x01)
      (OGPH_FLAG_WEIGHTED  0x02)
      (OGPH_FLAG_TYPED     0x04)
      (OGPH_FLAG_EMBEDDED  0x08)
      (OGPH_EDGE_FLAG_LOW_CONFIDENCE 0x01))
    (functions
      (graph-open-close
        (tgph_open          (path:const-char*) -> tgph_handle_t
          (note "mmap open; returns NULL on error"))
        (tgph_open_logical  (logical_name:const-char*) -> tgph_handle_t
          (note "resolves 'dbname.graphname' -> {tde_base_path}/dbname/graphname.ogph; sxp_path parameter REMOVED 2026-05-29"))
        (tgph_save          (h:tgph_handle_t path:const-char*) -> int
          (note "atomic write-then-rename"))
        (tgph_save_logical  (h:tgph_handle_t logical_name:const-char*) -> int)
        (tgph_close         (h:tgph_handle_t) -> void))
      (graph-meta
        (tgph_node_count        (h:tgph_handle_t) -> uint32_t)
        (tgph_edge_count_total  (h:tgph_handle_t) -> uint32_t)
        (tgph_flags             (h:tgph_handle_t) -> uint32_t
          (note "returns OGPH_FLAG_* bitmask"))
        (tgph_embed_dim         (h:tgph_handle_t) -> uint32_t
          (note "0 if no embeddings stored")))
      (graph-node
        (tgph_find_node    (h:tgph_handle_t label:const-char*) -> uint32_t
          (note "O(1) FNV-1a hash lookup; returns UINT32_MAX if not found"))
        (tgph_node_id      (h:tgph_handle_t idx:uint32_t) -> uint32_t)
        (tgph_node_type    (h:tgph_handle_t idx:uint32_t) -> uint32_t)
        (tgph_node_label   (h:tgph_handle_t idx:uint32_t) -> const-char*
          (note "zero-copy pointer into mmap; valid until tgph_close"))
        (tgph_node_flags   (h:tgph_handle_t idx:uint32_t) -> uint32_t)
        (tgph_node_embed   (h:tgph_handle_t idx:uint32_t out_dim:uint32_t*) -> const-float*
          (note "zero-copy; NULL if embed_dim=0")))
      (graph-edge
        (tgph_next_edge   (h:tgph_handle_t node_idx:uint32_t edge_cursor:uint32_t*
                            out_target:uint32_t* out_type:uint32_t* out_weight:float*) -> int
          (note "CSR iteration; init cursor to 0; returns 0 when no more edges; O(degree)"))
        (tgph_out_degree  (h:tgph_handle_t node_idx:uint32_t) -> uint32_t))
      (graph-builder
        (tgph_builder_create           (embed_dim:int flags:uint32_t) -> tgph_builder_t*
          (note "embed_dim=0 means no embeddings; returns NULL on OOM"))
        (tgph_builder_add_node         (b:tgph_builder_t* label:const-char* type:uint32_t
                                          flags:uint32_t embed:const-float*) -> uint32_t
          (note "embed may be NULL if embed_dim=0; returns 0-based node index; UINT32_MAX on error"))
        (tgph_builder_add_edge         (b:tgph_builder_t* from_idx:uint32_t to_idx:uint32_t
                                          edge_type:uint32_t weight:float flags:uint32_t) -> int)
        (tgph_builder_finalise         (b:tgph_builder_t* path:const-char*) -> int
          (note "writes CSR graph to file atomically; frees builder; do not call tgph_builder_free after this"))
        (tgph_builder_finalise_logical (b:tgph_builder_t* logical_name:const-char*) -> int)
        (tgph_builder_free             (b:tgph_builder_t*) -> void
          (note "call only when finalise was not used or failed")))))
  (capabilities
    (tabular-io    "ODAT: row/column CRUD with typed cells (INT32/INT64/FLOAT/STRING/BLOB)")
    (vocabulary    "OVOC: O(1) word<->token hash table with embedded float vectors and 64-bit flags")
    (vector-store  "OVEC: mmap float matrix; brute-force cosine top-k search")
    (graph-store   ".OGPH: CSR graph with O(1) node lookup and O(degree) edge traversal; builder for construction")
    (logical-names "dbname.tablename resolution to filesystem path")
    (file-locking  "platform abstraction in platform.c")
    (atomic-write  "write-then-rename in tde_save/tde_save_logical and tgph_builder_finalise")
    (csv-export    "tde_export_csv"))
  (directive-divergences
    (divergence
      id "TH-01"
      severity HIGH
      description "sexp version was '1.1' but source VERSION_MAJOR=1/VERSION_MINOR=0 and CMakeLists.txt project version '1.0'"
      resolution "corrected to '1.0' in 2026-05-29 update")
    (divergence
      id "TH-02"
      severity MEDIUM
      description "tgph_save and tgph_save_logical were absent from sexp functions section despite being exported in .def and implemented"
      file "src/tharavu.def:63-64, src/include/ogph.h:35-36"
      resolution "added to graph-api section in 2026-05-29 update")
    (divergence
      id "TH-03"
      severity CRITICAL
      description "tde_config_load() listed in sexp public-api but was REMOVED from tharavu_dll.h in THARAVU v1.2 (2026-05-29); sexp version still said '1.0'"
      file "src/include/tharavu_dll.h:173"
      actual "tde_config_load() removed; migration comment added; version bumped to 1.2 in CMakeLists.txt"
      resolution "FIXED in this update: version corrected 1.0->1.2; tde_config_load removed from public-api; tde_get_base_path/tde_set_dim/tde_set_hash_cap added; tgph_open_logical sxp_path param removed")
    (divergence
      id "TH-04"
      severity MEDIUM
      description "tgph_open_logical sxp_path parameter listed in sexp but removed from actual header (tde_get_base_path replaces sxp_path resolution)"
      file "src/include/ogph.h"
      resolution "FIXED in this update: tgph_open_logical updated to no sxp_path parameter"))
  (meta
    (audit-verified "2026-05-31")
    (audit-status   PARTIAL)
    (audit-notes    "THARAVU v1.2: tde_config_load removed; tde_get_base_path/tde_set_dim/tde_set_hash_cap added; CMakeLists VERSION 1.2; all tgph_*/tgph_builder_* functions confirmed; build artifacts present (libtharavu.dll libtharavu.dll.a libtharavu.a de_crud.exe de_import.exe de_dump.exe)")))
