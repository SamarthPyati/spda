# Changelog

All notable changes to spda are documented here. The project follows
[Semantic Versioning](https://semver.org/).

## [2.0.0] — 2026-11-24

A rewrite focused on safety. 2.0 is **not source- or binary-compatible** with
1.x; see [Migrating from 1.x](#migrating-from-1x) below.

### Added

- **Single-header distribution.** Define `SPDA_IMPLEMENTATION` in one `.c` file, or `SPDA_STATIC` for a private copy per file.
- **Error reporting.** `spda_last_error()` and `spda_strerror()` expose a per-thread `spda_err_t` code that every call sets (`SPDA_OK` on success).
- **Failure logging.** Failures are logged to stderr by default. `SPDA_NO_LOG` silences the log, and a custom `SPDA_LOG(err, func, msg)` redirects it.
- **Pluggable allocator.** Define `SPDA_MALLOC`, `SPDA_REALLOC` and `SPDA_FREE`.
- **New removal functions:**
  - `spda_swap_remove` and `spda_swap_remove_into`: O(1) removal that doesn't keep order.
  - `spda_remove_range(a, idx, n)`.
  - `spda_remove_into` and `spda_pop_into`, which copy out the removed element.
- **Generic printing.** `spda_print(a)` and `spda_fprint(out, a)` choose a printer for every standard type via `_Generic`. `spda_print_with` and `spda_fprint_with` take a custom printer, and the built-in `spda_fmt_*` printers are public.
- **Version macros.** `SPDA_VERSION_MAJOR`, `_MINOR`, `_PATCH`, `SPDA_VERSION` and `SPDA_VERSION_STRING`.
- **`SPDA_COUNTOF(arr)`** for counting the elements of a C array.
- **Test suite.** 200+ tests, each run in its own process, with sanitizer builds (`make DEBUG=1 test`) and simulated allocation failures.

### Changed

- **Failed operations never lose the array.** On any failure, functions that return the array return the one they were given, unchanged. `_spda_resize`, `_spda_resize_def` and `spda_shrink_to_fit` no longer return `NULL`. Only `spda_create`, `spda_reserve` and `spda_copy` return `NULL`.
- **Removal functions return `bool`:** `spda_remove`, `spda_pop` and the rest return `true` on success. `spda_set_length` also returns `bool`, and refuses a length above the capacity.
- **Indices are `size_t`** (they were `int`), so negative indices are rejected as out of bounds.
- **Functions instead of macros:** `spda_destroy`, `spda_clear`, `spda_set_length` and `spda_reverse` are now real functions, and safe to call on `NULL` or invalid arrays.
- **Print format:** printing produces `[1, 2, 3]` (strings quoted, `NULL` shown as `NULL`) instead of space-separated values.
- **Larger header:** the array header grew from 24 to 32 bytes on 64-bit platforms. It is now aligned for any standard element type and holds a signature that identifies live arrays.
- **Macros evaluate each argument exactly once,** so `spda_append(arrs[i++], x)` is safe. `spda_foreach` fixes its end pointer when the loop starts.
- **`spda_reverse` never allocates.**
- **`spda_sort` no longer calls `exit()`** on a `NULL` array. It reports an error instead.

### Removed

- **`spda.c`.** Everything lives in `spda.h`.
- **The old printing API:**
  - `spda_print(array, printer)`. Use `spda_print(a)` or `spda_print_with(a, fn)`.
  - The `printInt`, `printFloat`, `printDouble`, `printChar` and `printStr` printers.
  - `spda_print_metadata`.
- **Random-number helpers:** `randint`, `randfloat`, `spda_rand` and `spda_randf`.
- **`spda_search`,** which was declared but never implemented.
- **`CARRAY_LEN`.** Renamed to `SPDA_COUNTOF`.
- **`SPDA_SHRINK_THRESHOLD`,** which was never used.
- **The `-lm` requirement.**

### Fixed

- **Endless loop:** `spda_append_many` on a capacity-0 array (for example after `_spda_resize(a, 0)`).
- **Heap overflows** from `capacity * size` and `length + count` wrapping around.
- **Use-after-free** when appending an array to itself (`spda_append_items(a, a, n)`).
- **Misaligned elements** on x86-64 (`long double`, `__int128`, SIMD types).
- **Length above capacity:** `_spda_resize` to a capacity below the length left `length > capacity`, so later reads and writes went out of bounds. The length is now cut down, but only after the reallocation succeeds.
- **Lost arrays on failure:** a failed `realloc` in a resize or shrink returned `NULL`, so `a = ...` lost the array (and leaked it).
- **Crash on `NULL` destination:** `spda_remove_ret` crashed when `dest` was `NULL`. `spda_remove_into` accepts `NULL`.
- **Double frees:** a double `spda_destroy` is now detected instead of freeing twice.

## Migrating from 1.x

| 1.x | 2.0 |
|---|---|
| Compile `spda.c`, link `-lm` | `#define SPDA_IMPLEMENTATION` in one `.c` file before `#include "spda.h"`; no `-lm` |
| `spda_print(a, printInt)` | `spda_print(a)` |
| `spda_print(a, myPrinter)` with `void myPrinter(void *elem)` | `spda_print_with(a, myPrinter)` with `void myPrinter(FILE *out, const void *elem)` |
| `spda_print_metadata(a)` | `printf("%zu %zu %zu\n", spda_len(a), spda_cap(a), spda_stride(a))` |
| `spda_pop_ret(a, &out)` | `spda_pop_into(a, &out)` |
| `spda_remove_ret(a, i, &out)` | `spda_remove_into(a, i, &out)` |
| `a = spda_remove(a, i)` | `spda_remove(a, i)`, which returns `bool`; don't assign it |
| `CARRAY_LEN(arr)` | `SPDA_COUNTOF(arr)` |
| `_spda_destroy(a)` | `spda_destroy(a)` |
| `randint`, `spda_rand`, ... | Write your own; for example a loop of `spda_append(a, my_rand())` |
| Negative `int` index | Use `size_t` indices; out-of-range values are reported with `SPDA_ERR_OUT_OF_BOUNDS` |
| Checking `a = _spda_resize(a, n)` for `NULL` | Check `spda_last_error() != SPDA_OK`; `a` is never `NULL`-ed |

**Binary compatibility:** because the header layout changed, arrays created by
1.x code cannot be passed to 2.0 code, and a 2.0 `libspda` cannot replace a 1.x
one without recompiling.

[2.0.0]: https://github.com/SamarthPyati/spda/releases/tag/v2.0.0
