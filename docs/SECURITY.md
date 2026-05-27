# Security Notes — Tharavu Data Engine

## Multi-Process Write Safety

All three write paths use a sidecar `.lock` file to serialise concurrent writes:

| Write function | Format | Lock acquired |
|---|---|---|
| `tde_save` / `de_save` | `.odat` | Yes — `{file}.lock` |
| `tde_build_vocab` / `de_build_vocab` | `.ovoc` | Yes — `{file}.lock` |
| `tde_build_vectors` / `tde_build_vectors_flat` | `.ovec` | Yes — `{file}.lock` |

**Mechanism:**
- **Windows:** `LockFileEx` (blocking, kernel-enforced, process-level)
- **POSIX (Linux / macOS):** `fcntl F_SETLK` (advisory, process-level)

**Atomicity:** Each write goes to `{file}.tmp` first, then atomically renames to the target
(`MoveFileExA` on Windows, `rename` on POSIX).  A reader that opens the file concurrently
always sees either the prior complete version or the new complete version — never a partial
write.

**POSIX advisory locking caveat:** `fcntl` locks are cooperative.  Any process that bypasses
the tharavu API and writes directly to a `.odat`/`.ovec`/`.ovoc` file will not respect the
lock.  The guarantee holds only when all writers go through the tharavu write functions.

---

## Known Limitations

None that block production use.  The advisory-locking caveat above is inherent to the POSIX
`fcntl` design and cannot be changed without mandatory locking (a kernel-level feature not
available on all Linux configurations).

---

## Reporting Vulnerabilities

Please open a private issue or contact the maintainer directly before disclosing any
vulnerability publicly.
