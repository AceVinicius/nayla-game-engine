---
paths:
  - "src/**"
  - "tests/**"
  - "cmake/**"
  - "tools/**"
  - "**/CMakeLists.txt"
  - ".github/workflows/**"
  - "docs/**"
---

# Known Defect Classes

Defects that reached review or Copilot more than once. Implementers check each class that applies to their change **before reporting**; the reviewer checks them all. Finding them yourself is cheaper than a fix round.

## C and tests
- **Lifetime**: no use of a pointer after its object is freed or shut down, not even `ptr != NULL` (the value is indeterminate).
- **Vacuous tests**: poison out-parameters before a call that should fail; make sure a poison or default value cannot satisfy the check; counters must not be cumulative across cycles; show the subject ran (e.g. the injected allocator was used).
- **Padding**: no `memcmp` over structs that were assigned; compare fields.
- **Portability**: GCC vs Clang vs MSVC warnings, C17 vs C23 constructs, feature-test macros, process-global library state.

## Checks and scripts (grep, CMake, shell, CI)
- **Line-based greps**: multi-declarator lines, parameter lists, indentation, comments, case, `#import`/`@import`; grep exit status 2 (error) counted as "no match".
- **Exit status**: every command whose failure matters is checked, including `mktemp`, `cp` and `gh`; `die`/`exit` inside a helper aborts the whole script, so helpers whose failure is a result must `return`.
- **Empty output is unknown, never PASS**: an API or network error usually prints nothing.
- **Pagination**: `gh api` returns one page unless `--paginate`.
- **Renames**: `git diff --name-only` reports only a rename's destination unless `--no-renames`.

## Waits and timeouts
- Events already queued but not returned; retryable errors (`EINTR`, `EAGAIN`) treated as fatal; bounds that cover processing time, not just blocking.

## Specs and ADRs
- A statement elsewhere in the document that the change makes false; numbers or issue references that drifted.
- **Third-party behaviour** (OS, library, protocol) is cited from upstream documentation or source, with a link. Not from memory: these claims were wrong in review more than once (libdecor fallback, `XInitThreads`, Xlib's default I/O handler).
