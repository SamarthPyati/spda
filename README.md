# spda.h

A **single-header, generic dynamic array** for C. You work with a plain typed
pointer (`int *`, `Point *`, ...), index it with `a[i]`, and the library takes care of the rest.

**Why:** growable arrays in C usually mean hand-written `realloc` code with a
separate length and capacity for every array, or a `void *` container that loses
the element type. With spda the array *is* a `T *`, so it works with `a[i]`, with
pointer arithmetic, and with any function that takes `T *`. The length and capacity
travel with it, and every operation is bounds- and overflow-checked.

> **Current version: 2.0.0.** Upgrading from 1.x? 2.0 has breaking changes. See
> the [migration guide](CHANGELOG.md#migrating-from-1x) in the [changelog](CHANGELOG.md).

## Contents

- [Quick start](#quick-start)
- [Features](#features) · [Requirements](#requirements) · [Installation](#installation)
- [Usage](#usage) · [Error handling](#error-handling) · [Caveats](#caveats)
- [API reference](#api-reference) · [Configuration](#configuration) · [Memory layout](#memory-layout)
- [Versioning](#versioning) · [Testing](#testing) · [Contributing](#contributing)

## Quick start

Copy `spda.h` next to a new `main.c`:

```c
#include <stdio.h>
#define SPDA_IMPLEMENTATION   // in exactly one .c file
#include "spda.h"

int main(void) {
    int *a = spda_create(int);
    for (int i = 1; i <= 5; i++)
        spda_append(a, i * i);

    spda_print(a);
    printf("length %zu, a[2] = %d\n", spda_len(a), a[2]);

    spda_destroy(a);
    return 0;
}
```

```sh
cc -std=c17 -o main main.c && ./main
```

```
[1, 4, 9, 16, 25]
length 5, a[2] = 9
```

## Features

- **Generic**: any element type, including structs.
- **Plain pointer access**: `a[i]`, pointer arithmetic, and passing to functions that take `T *`.
- **Single header**: drop `spda.h` into your project; no build step required.
- **Safe failures**: bounds and size-overflow checks; a failed operation never loses or corrupts the array.
- **Error reporting**: per-thread error codes, plus a log line on stderr for every failure.
- **Pluggable allocator**: route all allocations through your own `malloc` / `realloc` / `free`.
- **Aligned storage**: elements are aligned for every standard type, including `long double` and `__int128`.

## Requirements

- **C11** (`_Generic`, `_Alignas`, `_Static_assert`, `_Thread_local`).
- **`__typeof__`**, used by the insertion macros: GCC and Clang provide it in every C mode, and C23 compilers provide it as `typeof`.
- **MSVC's C compiler is not supported.** Use Clang (including `clang-cl`) or GCC/MinGW on Windows.

## Installation

### Single header (recommended)

Copy `spda.h` into your project. In **exactly one** `.c` file, define
`SPDA_IMPLEMENTATION` before including it. Every other file includes it normally:

```c
// spda_impl.c (or your main.c)
#define SPDA_IMPLEMENTATION
#include "spda.h"
```

```c
// any other file
#include "spda.h"
```

To give each translation unit its own private copy instead, define `SPDA_STATIC`
before every include. This implies `SPDA_IMPLEMENTATION` and makes every function `static`.

### Shared library

```sh
git clone https://github.com/SamarthPyati/spda
cd spda
make build_lib        # bin/release/libspda.so (or libspda.dylib on macOS)
```

Then include `spda.h` **without** `SPDA_IMPLEMENTATION` and link the library:

```sh
cc -std=c17 -o my_program my_program.c -Ispda -Lspda/bin/release -lspda
```

## Usage

### Basic example

```c
#include <stdio.h>
#define SPDA_IMPLEMENTATION
#include "spda.h"

int main(void) {
    int items[] = {3, 48, 1, 34, 0, 12};

    int *a = spda_create(int);

    spda_append(a, 7);                                 // [7]
    spda_append_many(a, 1, 2, 3);                      // [7, 1, 2, 3]
    spda_append_items(a, items, SPDA_COUNTOF(items));  // [7, 1, 2, 3, 3, 48, 1, 34, 0, 12]
    spda_print(a);

    spda_insert(a, 0, -1);                             // insert before index 0
    spda_remove(a, 1);                                 // drop the 7

    int last;
    spda_pop_into(a, &last);                           // last == 12
    printf("popped %d, length %zu, capacity %zu\n", last, spda_len(a), spda_cap(a));

    spda_foreach(int, it, a) *it *= 2;                 // modify in place
    spda_print(a);

    spda_destroy(a);
    return 0;
}
```

Output:

```
[7, 1, 2, 3, 3, 48, 1, 34, 0, 12]
popped 12, length 9, capacity 32
[-2, 2, 4, 6, 6, 96, 2, 68, 0]
```

### Structs and custom printers

```c
#include <stdio.h>
#define SPDA_IMPLEMENTATION
#include "spda.h"

typedef struct { double x, y, z; } Point3D;

static void print_point(FILE *out, const void *elem) {
    const Point3D *p = elem;
    fprintf(out, "(%.1f, %.1f, %.1f)", p->x, p->y, p->z);
}

static int by_x(const void *a, const void *b) {
    double x = ((const Point3D *)a)->x, y = ((const Point3D *)b)->x;
    return (x > y) - (x < y);
}

int main(void) {
    Point3D *pts = spda_create(Point3D);
    spda_append_many(pts, (Point3D){7, 8, 9}, (Point3D){1, 2, 3}, (Point3D){4, 5, 6});

    spda_sort(pts, by_x);
    spda_print_with(pts, print_point);   // [(1.0, 2.0, 3.0), (4.0, 5.0, 6.0), (7.0, 8.0, 9.0)]

    spda_destroy(pts);
    return 0;
}
```

### Strings

The array stores **pointers**: it never copies or frees the strings. String
literals are always safe. Buffers you own must outlive the array.

```c
const char **names = spda_create(const char *);
spda_append_many(names, "pear", "apple", "fig");
spda_print(names);                       // ["pear", "apple", "fig"]
spda_destroy(names);
```

Prefer `const char *` over `char *` for literals: writing through a literal is undefined behaviour.

## Error handling

Every function that can fail records the outcome of its **most recent call** in a
per-thread error code, which is `SPDA_OK` on success. On failure:

- **Nothing is modified.** Functions that return the array return the one they were given, so `a = ...` never loses it.
- `spda_create`, `spda_reserve` and `spda_copy` return `NULL`.
- Functions returning `bool` return `false`.
- A line is written to stderr, e.g. `spda: _spda_pop: Cannot pop from an empty array. (index out of bounds)`.

```c
spda_append(a, x);
if (spda_last_error() != SPDA_OK)
    fprintf(stderr, "append failed: %s\n", spda_strerror(spda_last_error()));
```

| Code | `spda_strerror` | Typical cause |
|---|---|---|
| `SPDA_OK` | no error | success |
| `SPDA_ERR_NOMEM` | out of memory | allocation failed |
| `SPDA_ERR_INVALID_ARRAY` | invalid array | `NULL`, destroyed, or not created by spda |
| `SPDA_ERR_INVALID_ARG` | invalid argument | `NULL` value / comparator / stream / printer, zero element size |
| `SPDA_ERR_OUT_OF_BOUNDS` | index out of bounds | bad index or range, popping an empty array, length above capacity |
| `SPDA_ERR_OVERFLOW` | size overflow | requested size does not fit in `size_t` |

The accessors (`spda_len`, `spda_cap`, `spda_stride`) and the `spda_fmt_*`
printers never change the error code. `spda_destroy(NULL)` is a no-op, like `free(NULL)`.

**Controlling the log** (define before including `spda.h`):

- `#define SPDA_NO_LOG`: silence it. Error codes are still recorded.
- `#define SPDA_LOG(err, func, msg) my_logger(err, func, msg)`: send it elsewhere.

**Invalid pointers.** Each array's header carries a signature, so `NULL`, destroyed
arrays, and ordinary buffers are rejected with `SPDA_ERR_INVALID_ARRAY` instead of
corrupting memory. This is best effort: checking still reads the bytes just before
the pointer, so passing a pointer with no readable memory in front of it is
undefined behaviour.

## Caveats

### Don't change the length inside `spda_foreach`

`spda_foreach` walks raw pointers into the array, so the array must not move or
shrink while it runs:

- **Allowed**: reading and writing elements (`*it = ...`), and reordering them (`spda_sort`, `spda_reverse`).
- **Not allowed**: append / insert, because a reallocation moves the elements and the loop pointer then reads freed memory. Elements appended while there is spare capacity are not visited either, since the end is fixed when the loop starts.
- **Not allowed**: remove / pop / swap-remove, because elements shift down, one is skipped, and the loop runs past the new length.

To change the length while iterating, use an index loop through the array variable:

```c
// Appending: re-reads `a` and its length on every pass
for (size_t i = 0; i < spda_len(a); i++)
    if (a[i] > 10) spda_append(a, a[i] / 2);

// Removing: go backwards so shifted elements are ones already visited
for (size_t i = spda_len(a); i-- > 0; )
    if (a[i] < 0) spda_remove(a, i);
```

The same applies to any `T *p = &a[i]` you keep: a reallocation invalidates it.

### `spda_set_length` does not initialise elements

`spda_set_length` can raise the length up to the capacity, but the newly exposed
elements contain **whatever was in memory**. Write them before reading them.
Lowering the length (or `spda_clear`) is always safe.

### Macro arguments

The insertion macros assign the possibly reallocated pointer back to `array`, so
`array` must be an **assignable variable or expression** (`a`, `arrs[i]`, `s->items`), not a
function result or a cast. Every argument is evaluated exactly once, so
`spda_append(arrs[i++], x)` is safe.

## API reference

`T` is the element type and `a` is a `T *` returned by `spda_create` / `spda_reserve`.

### Creation and destruction

| Call | Description |
|---|---|
| `spda_create(T)` | New empty array (default capacity `SPDA_DEFAULT_CAPACITY`, 32). |
| `spda_reserve(T, capacity)` | New empty array with at least `capacity` slots. |
| `spda_copy(a)` | Independent copy, or `NULL`. |
| `spda_destroy(a)` | Free the array. `NULL` is a no-op. |

### Adding elements (macros, assign back to `a`)

| Call | Description |
|---|---|
| `spda_append(a, value)` | Append one value. |
| `spda_append_many(a, v1, v2, ...)` | Append several values. |
| `spda_append_items(a, items, count)` | Append `count` elements from a C array, or from `a` itself. |
| `spda_insert(a, idx, value)` | Insert before `idx` (`idx == length` appends). |

### Removing elements (return `bool`)

| Call | Description |
|---|---|
| `spda_pop(a)` / `spda_pop_into(a, &out)` | Remove the last element (optionally copying it out). |
| `spda_remove(a, idx)` / `spda_remove_into(a, idx, &out)` | Remove at `idx`, shifting later elements down. |
| `spda_remove_range(a, idx, n)` | Remove `n` elements starting at `idx`. Rejected whole if it runs past the end. |
| `spda_swap_remove(a, idx)` / `spda_swap_remove_into(a, idx, &out)` | O(1) remove: move the last element into the gap. Order not kept. |
| `spda_clear(a)` | Length to 0, capacity kept. |
| `spda_set_length(a, n)` | Set length directly (`n <= capacity`). See [caveat](#spda_set_length-does-not-initialise-elements). |

### Information

| Call | Description |
|---|---|
| `spda_len(a)` | Number of elements (0 for invalid arrays). |
| `spda_cap(a)` | Capacity in elements. |
| `spda_stride(a)` | Size of one element in bytes. |

### Ordering and iteration

| Call | Description |
|---|---|
| `spda_sort(a, cmp)` | Sort with a `qsort`-style comparator. |
| `spda_reverse(a)` | Reverse in place (never allocates). |
| `spda_foreach(T, it, a) { ... }` | Loop with `it` as a `T *` to each element. See [caveat](#dont-change-the-length-inside-spda_foreach). |

### Memory

| Call | Description |
|---|---|
| `a = spda_shrink_to_fit(a)` | If under 25% full, shrink to twice the length. |
| `a = _spda_resize(a, capacity)` | Set the capacity exactly (truncates the length if smaller). |

### Printing

| Call | Description |
|---|---|
| `spda_print(a)` | Print to stdout with a newline, e.g. `[1, 2, 3]`. Standard types only. |
| `spda_fprint(out, a)` | Same, to any stream, no newline. |
| `spda_print_with(a, fn)` / `spda_fprint_with(out, a, fn)` | Use a custom printer `void fn(FILE *, const void *elem)`. |

Built-in printers are chosen automatically for `bool`, all `char` and integer
types, `float`, `double`, `long double`, `char *` and `const char *`. They are
also available as `spda_fmt_int`, `spda_fmt_size`, and so on. Other types (structs) need `spda_print_with`.

### Errors

| Call | Description |
|---|---|
| `spda_last_error()` | Outcome of the last spda call on this thread. |
| `spda_strerror(err)` | Short description of an error code. |

## Configuration

Define before including `spda.h`:

| Macro | Effect |
|---|---|
| `SPDA_IMPLEMENTATION` | Emit the function definitions (in one `.c` file). |
| `SPDA_STATIC` | Private `static` copy per translation unit. |
| `SPDA_DEFAULT_CAPACITY` | Initial / minimum capacity (default 32). |
| `SPDA_NO_LOG` | Don't log failures to stderr. |
| `SPDA_LOG(err, func, msg)` | Custom failure logger. |
| `SPDA_MALLOC(size)`, `SPDA_REALLOC(ptr, size)`, `SPDA_FREE(ptr)` | Custom allocator. Define all three or none. |
| `SPDA_THREAD_LOCAL` | Storage class for the per-thread error code (default `_Thread_local`). |

## Memory layout

```
                     HEADER                                       ELEMENTS
 +-------------------------------------------------+-----------------------------+
 |  capacity  |  length  |  stride  |    magic     |  a[0]  a[1]  ...            |
 +-------------------------------------------------+-----------------------------+
 |  size_t    |  size_t  |  size_t  |    size_t    |                             |
 +-------------------------------------------------+-----------------------------+
                                                   ^
                                       pointer returned to you
```

The header is 32 bytes on 64-bit platforms (16 on 32-bit), which keeps elements
aligned to `max(alignof(max_align_t), 16)`. `magic` is the signature that
identifies live arrays. Types aligned to more than 16 bytes (`_Alignas(32)`, AVX
vectors) are not supported.

## Versioning

spda follows [Semantic Versioning](https://semver.org/), and the header exposes its version:

```c
#if SPDA_VERSION >= 20000                // 2.0.0 or newer
printf("spda %s\n", SPDA_VERSION_STRING); // "2.0.0"
#endif
```

`SPDA_VERSION_MAJOR`, `SPDA_VERSION_MINOR` and `SPDA_VERSION_PATCH` are also
available. Changes in each release are listed in [CHANGELOG.md](CHANGELOG.md).

## Testing

```sh
make test             # build and run the suite
make test T=reverse   # only tests whose name contains "reverse"
make DEBUG=1 test     # same, with AddressSanitizer + UndefinedBehaviorSanitizer
```

Each test runs in its own process with a timeout, so a crash or hang fails only
that test. `SPDA_TEST_NOFORK=1` runs them in-process for debugging with lldb/gdb.

## Contributing

1. **Run both test modes before sending a change.** `make test` and
   `make DEBUG=1 test` must both pass with no compiler warnings.
2. **Add a test for every fix or feature.** Write `TEST(group, name) { ... }`
   anywhere in `tests/test.c` and it registers itself. To exercise out-of-memory
   paths, call `fail_alloc_after(0)` so the next allocation fails.
3. **Follow the library's conventions:**
   - Internal names use the `_spda_` prefix.
   - Every function that can fail reports through `SPDA_ERR(code, msg)` and calls
     `_spda_clear_error()` on success.
   - On failure, leave the array untouched and return the array that was passed in.
   - Allocate only through `SPDA_MALLOC` / `SPDA_REALLOC` / `SPDA_FREE`.
4. **Keep the docs current.** Update this README when the API changes, and keep
   its examples compiling.

Known issues and agreed future work are tracked in [`ISSUES.md`](ISSUES.md).

## License

This project is licensed under the [MIT License](LICENSE).

## Acknowledgments

- Inspired by the [dynamic array implementation](https://github.com/travisvroman/kohi/blob/main/kohi.core/src/containers/darray.h) in Travis Vroman's [Kohi](https://github.com/travisvroman/kohi) game engine.
- Single-header design in the style of Sean Barrett's [stb](https://github.com/nothings/stb) libraries, notably [stb_ds.h](https://github.com/nothings/stb/blob/master/stb_ds.h).
