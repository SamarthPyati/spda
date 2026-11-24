/*
**  @brief: A Robust Dynamic Array Implementation in C **
*   @Author: Samarth Pyati 
*   @Date : 08-11-2026
*   @Version: 2.0
*
*   Change Notes: 
*       1. Making this library as a single header implementation
* 
* * Memory Layout *
* size_t capacity = max elements that array can contain;
* size_t length = number of elements the array actually contains;
* size_t stride = size of each items in array;
* void *elements = elements in array;
*
*  Structure of the array: 
*                 METADATA                                 ELEMENTS
*  +---------------------------------------------+---------------------------------+
*  |  capacity   |  length   |  stride   | (pad) |            elements             |
*  +---------------------------------------------+---------------------------------+
*  |  (size_t)   |  (size_t) |  (size_t) |       |            (void *)             |
*  +---------------------------------------------+---------------------------------+
*                                                ^
*                                            array pointer
*
*  The header is padded to a multiple of max(alignof(max_align_t), 16) bytes
*  (32 on 64-bit platforms) so elements of any standard type are aligned.
*/

#ifndef SPDA_HEADER_GUARD_H_
#define SPDA_HEADER_GUARD_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct {
    _Alignas(max_align_t) _Alignas(16) size_t capacity;
    size_t length;
    size_t stride;
    size_t magic;
} spda_header_t;

#define SPDA_MAGIC ((size_t)0x5350444153504441ull)

_Static_assert(sizeof(spda_header_t) % _Alignof(max_align_t) == 0,
               "spda: header size must preserve max_align_t alignment of elements");
_Static_assert(sizeof(spda_header_t) % 16 == 0,
               "spda: header size must preserve 16-byte alignment of elements");
_Static_assert(sizeof(spda_header_t) == (4 * sizeof(size_t) + 15) / 16 * 16,
               "spda: magic must fit in the header's alignment padding");

#ifndef SPDA_DEFAULT_CAPACITY
    #define SPDA_DEFAULT_CAPACITY 32
#endif 

#if defined(SPDA_STATIC) && !defined(SPDA_IMPLEMENTATION)
	#define SPDA_IMPLEMENTATION
#endif

#ifdef SPDA_STATIC
	#if defined(__GNUC__) || defined(__clang__)
		#define SPDA_DEF static __attribute__((unused))
	#else
		#define SPDA_DEF static
	#endif
#else
	#define SPDA_DEF extern
#endif

#define SPDA_GROWTH_FACTOR 2

/* ERROR HANDLING
 *
 * Every function that can fail records the outcome of its most recent call
 * in a per-thread error code: SPDA_OK on success, otherwise the reason.
 * Read it with spda_last_error(); spda_strerror() turns it into text.
 *
 *     spda_append(a, x);
 *     if (spda_last_error() != SPDA_OK) { ... }
 *
 * On failure nothing is modified. Functions returning the array return the
 * one they were given, _spda_create / spda_copy return NULL, and functions
 * returning bool return false. The accessors (spda_len, spda_cap,
 * spda_stride) and the spda_fmt_* printers never change the error code.
 */
typedef enum {
    SPDA_OK = 0,
    SPDA_ERR_NOMEM,          // memory allocation failed
    SPDA_ERR_INVALID_ARRAY,  // array is NULL, destroyed, or not created by spda
    SPDA_ERR_INVALID_ARG,    // another argument is invalid (NULL pointer, zero element size)
    SPDA_ERR_OUT_OF_BOUNDS,  // index, range or length outside the array
    SPDA_ERR_OVERFLOW,       // requested size does not fit in size_t
} spda_err_t;

SPDA_DEF spda_err_t spda_last_error(void);           // outcome of the last spda call on this thread
SPDA_DEF const char *spda_strerror(spda_err_t err);  // short description of an error code

/* Diagnostics: every failure is logged to stderr by default. Define
   SPDA_NO_LOG to silence it (the error code is still recorded), or define
   SPDA_LOG(err, func, msg) yourself to route it elsewhere. */
#ifndef SPDA_LOG
    #ifdef SPDA_NO_LOG
        #define SPDA_LOG(err, func, msg) ((void)(err), (void)(func), (void)(msg))
    #else
        #define SPDA_LOG(err, func, msg) \
            fprintf(stderr, "spda: %s: %s (%s)\n", (func), (msg), spda_strerror(err))
    #endif
#endif

// Per-thread error storage. Override if your compiler lacks C11 _Thread_local. 
#ifndef SPDA_THREAD_LOCAL
    #define SPDA_THREAD_LOCAL _Thread_local
#endif

// Internal: record a failure (and log it) from inside a library function. 
#define SPDA_ERR(code, msg) _spda_set_error((code), __func__, (msg))

#define SPDA_COUNTOF(xs) (sizeof((xs)) / sizeof((xs)[0]))

// To use your own alloctors define the following 
#if !defined(SPDA_MALLOC) && !defined(SPDA_REALLOC) && !defined(SPDA_FREE)
    #define SPDA_MALLOC(size)       malloc(size)
    #define SPDA_REALLOC(ptr, size) realloc((ptr), (size))
    #define SPDA_FREE(ptr)          free(ptr)
#elif !defined(SPDA_MALLOC) || !defined(SPDA_REALLOC) || !defined(SPDA_FREE)
    #error "spda: define all SPDA_MALLOC, SPDA_REALLOC and SPDA_FREE, or neither of them"
#endif

/* Core Operations */
SPDA_DEF void *_spda_create(size_t cap, size_t stride);
SPDA_DEF void spda_destroy(void *array);

/* Field Operations */
static inline spda_header_t* _spda_get_header(const void *array) {
    return array ? (spda_header_t *)array - 1 : NULL;
}

// True for a live array created by spda. Best effort: it still reads the
// header bytes before `array`, so a pointer with no readable memory in front
// of it is undefined behaviour. In practice it rejects foreign buffers and
// destroyed arrays instead of letting them corrupt memory. 
static inline bool _spda_is_valid(const void *array) {
	if (array == NULL) return false;
	const spda_header_t *header = _spda_get_header(array);
	return header->magic == SPDA_MAGIC && header->stride > 0;
}

// Accessors return 0 for anything that is not a live spda array.
static inline size_t spda_len(const void *array) {
    return _spda_is_valid(array) ? _spda_get_header(array)->length : 0;
}

static inline size_t spda_cap(const void *array) {
    return _spda_is_valid(array) ? _spda_get_header(array)->capacity : 0;
}

static inline size_t spda_stride(const void *array) {
    return _spda_is_valid(array) ? _spda_get_header(array)->stride : 0;
}

/* Helpers */
static inline bool _spda_alloc_size(size_t count, size_t stride, size_t *bytes) {
	if (stride && count > (SIZE_MAX - sizeof(spda_header_t)) / stride) 
		return false;

	*bytes = sizeof(spda_header_t) + count * stride;
	return true;
}

/* Memory Operations */
SPDA_DEF void *_spda_resize_def(void *array);
SPDA_DEF void *_spda_resize(void *array, size_t size);
SPDA_DEF void *spda_shrink_to_fit(void *array);

/* Array Operations */
SPDA_DEF void *_spda_append(void *array, const void* value);
SPDA_DEF void *_spda_append_many(void *array, const void *items, size_t item_count);
SPDA_DEF void *_spda_insert(void *array, size_t idx, const void* value);

SPDA_DEF bool _spda_remove(void *array, size_t idx, size_t count, void *dest);
SPDA_DEF bool _spda_swap_remove(void *array, size_t idx, void *dest);

SPDA_DEF void spda_reverse(void *array);     // reverse array inplace

SPDA_DEF void spda_clear(void *array);
SPDA_DEF bool spda_set_length(void *array, size_t length);

/* Sort and search */
SPDA_DEF void spda_sort(void *array, int (*compar)(const void *, const void *));

/* Utilities */
SPDA_DEF void *spda_copy(void *src);

typedef void (*spda_elem_fn)(FILE *f, const void *elem);
SPDA_DEF void _spda_fprint(FILE *out, const void *array, spda_elem_fn pfn);

/* Default c datatypes element printer */
SPDA_DEF void spda_fmt_bool(FILE *o, const void *e);
SPDA_DEF void spda_fmt_char(FILE *o, const void *e);
SPDA_DEF void spda_fmt_schar(FILE *o, const void *e);
SPDA_DEF void spda_fmt_uchar(FILE *o, const void *e);
SPDA_DEF void spda_fmt_short(FILE *o, const void *e);
SPDA_DEF void spda_fmt_ushort(FILE *o, const void *e);
SPDA_DEF void spda_fmt_int(FILE *o, const void *e);
SPDA_DEF void spda_fmt_uint(FILE *o, const void *e);
SPDA_DEF void spda_fmt_long(FILE *o, const void *e);
SPDA_DEF void spda_fmt_ulong(FILE *o, const void *e);
SPDA_DEF void spda_fmt_llong(FILE *o, const void *e);
SPDA_DEF void spda_fmt_ullong(FILE *o, const void *e);
SPDA_DEF void spda_fmt_size(FILE *o, const void *e);   // not in spda_default_fmt; pass explicitly
SPDA_DEF void spda_fmt_float(FILE *o, const void *e);
SPDA_DEF void spda_fmt_double(FILE *o, const void *e);
SPDA_DEF void spda_fmt_ldouble(FILE *o, const void *e);
SPDA_DEF void spda_fmt_str(FILE *o, const void *e);

/* Macros */
#define spda_create(type) \
    (type *) _spda_create(SPDA_DEFAULT_CAPACITY, sizeof(type))

#define spda_reserve(type, capacity) \
    (type *) _spda_create(capacity, sizeof(type))

#define spda_append(array, value)                     \
    do {                                              \
        __typeof__(*(array)) spda__tmp_ = (value);    \
        (array) = _spda_append((array), &spda__tmp_); \
    } while (0)

#define spda_append_many(array, ...)                                                \
    do {                                                                            \
        __typeof__(*(array)) spda__tmp_[] = {__VA_ARGS__};                          \
        (array) = _spda_append_many((array), spda__tmp_, SPDA_COUNTOF(spda__tmp_)); \
    } while (0)

#define spda_append_items(array, items, count)                            \
    do {                                                                  \
            (array) = _spda_append_many((array), (items), (count));       \
    } while (0)

#define spda_foreach(type, varname, array) \
    for (type *varname = (array); varname != (array) + spda_len(array); ++varname)

#define spda_insert(array, idx, value)                       \
    do {                                                     \
        __typeof__(*(array)) spda__tmp_ = (value);           \
        (array) = _spda_insert((array), (idx), &spda__tmp_); \
    } while (0)

#define spda_remove(array, idx) 			_spda_remove((array), (idx), 1, NULL)
#define spda_remove_into(array, idx, dest)  _spda_remove((array), (idx), 1, (dest))
#define spda_remove_range(array, idx, n)	_spda_remove((array), (idx), (n), NULL)

#define spda_swap_remove(array, idx)             _spda_swap_remove((array), (idx), NULL)
#define spda_swap_remove_into(array, idx, dest)  _spda_swap_remove((array), (idx), (dest))

#define spda_pop(array) 					_spda_remove((array), spda_len(array) - 1, 1, NULL)
#define spda_pop_into(array, dest) 			_spda_remove((array), spda_len(array) - 1, 1, (dest))

/* size_t is always one of unsigned int / long / long long, so its coverred
   without its own entry (listing it too would be a duplicate type error).
   Other types (ex. structs) fail to compile: use spda_print_with. */
#define spda_default_fmt(array) _Generic((array)[0], \
	bool               : spda_fmt_bool,    \
	char               : spda_fmt_char,    \
	signed char        : spda_fmt_schar,   \
	unsigned char      : spda_fmt_uchar,   \
	short              : spda_fmt_short,   \
	unsigned short     : spda_fmt_ushort,  \
	int                : spda_fmt_int,     \
	unsigned int       : spda_fmt_uint,    \
	long               : spda_fmt_long,    \
	unsigned long      : spda_fmt_ulong,   \
	long long          : spda_fmt_llong,   \
	unsigned long long : spda_fmt_ullong,  \
	float              : spda_fmt_float,   \
	double             : spda_fmt_double,  \
	long double        : spda_fmt_ldouble, \
	char *             : spda_fmt_str,     \
	const char *       : spda_fmt_str)

#define spda_print(array) 			(_spda_fprint(stdout, (array), spda_default_fmt(array)), (void)putchar('\n'))
#define spda_print_with(array, pfn) (_spda_fprint(stdout, (array), (pfn)), (void)putchar('\n'))

#define spda_fprint(out, array)			  _spda_fprint((out), (array), spda_default_fmt(array))
#define spda_fprint_with(out, array, pfn) _spda_fprint((out), (array), (pfn))

#endif // SPDA_HEADER_GUARD_H_

#if defined(SPDA_IMPLEMENTATION) && !defined(SPDA_IMPLEMENTATION_GUARD_)
#define SPDA_IMPLEMENTATION_GUARD_

/* Error state */
static SPDA_THREAD_LOCAL spda_err_t _spda_last_err = SPDA_OK;

static void _spda_set_error(spda_err_t err, const char *func, const char *msg) {
	_spda_last_err = err;
	SPDA_LOG(err, func, msg);
}

static inline void _spda_clear_error(void) {
	_spda_last_err = SPDA_OK;
}

// Swap two non-overlapping blocks of n bytes through fixed 64 byte 
// buffer chunk by chunk instaed of reallocating every time
static inline void _spda_swap_bytes(char *a, char *b, size_t n) {
	char tmp[64];
	while (n > 0) {
		size_t k = (n < sizeof(tmp)) ? n : sizeof(tmp);
		memcpy(tmp, a, k);
		memcpy(a, b, k);
		memcpy(b, tmp, k);
		a += k;
		b += k;
		n -= k;
	}
}

SPDA_DEF spda_err_t spda_last_error(void) {
	return _spda_last_err;
}

SPDA_DEF const char *spda_strerror(spda_err_t err) {
	switch (err) {
		case SPDA_OK:                return "no error";
		case SPDA_ERR_NOMEM:         return "out of memory";
		case SPDA_ERR_INVALID_ARRAY: return "invalid array";
		case SPDA_ERR_INVALID_ARG:   return "invalid argument";
		case SPDA_ERR_OUT_OF_BOUNDS: return "index out of bounds";
		case SPDA_ERR_OVERFLOW:      return "size overflow";
	}
	return "unknown error";
}

/* Core */

SPDA_DEF void *_spda_create(size_t capacity, size_t stride) {
	if (capacity < SPDA_DEFAULT_CAPACITY)
		capacity = SPDA_DEFAULT_CAPACITY;

	if (stride == 0) {
		SPDA_ERR(SPDA_ERR_INVALID_ARG, "Stride (size of datatype) cannot be zero");
		return NULL;
	}

	size_t total_size;
	if (!_spda_alloc_size(capacity, stride, &total_size)) {
		SPDA_ERR(SPDA_ERR_OVERFLOW, "Capacity * element size exceeds SIZE_MAX.");
		return NULL;
	}

	spda_header_t *array = (spda_header_t *)SPDA_MALLOC(total_size);
	
	if (!array) {
		SPDA_ERR(SPDA_ERR_NOMEM, "Failed memory allocation for dynamic array.");
		return NULL;
	}
	
	array->capacity = capacity;
	array->length = 0;
	array->stride = stride;
	array->magic = SPDA_MAGIC;
	_spda_clear_error();
	return (void *)(array + 1);
}

SPDA_DEF void spda_destroy(void *array) {
	if (array == NULL) {   // like free(NULL): not an error
		_spda_clear_error();
		return;
	}

	if (!_spda_is_valid(array)) {
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer is not a live spda array (already destroyed?).");
		return;
	}

	spda_header_t *header = _spda_get_header(array);
	header->magic = 0;   // poison: a second destroy (or later use) is rejected
	SPDA_FREE(header);
	_spda_clear_error();
}

SPDA_DEF void *_spda_resize_def(void *array) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
		return array;
	}

	size_t cap = spda_cap(array);

	if (cap > SIZE_MAX / SPDA_GROWTH_FACTOR) {
		SPDA_ERR(SPDA_ERR_OVERFLOW, "Capacity cannot grow any further.");
		return array;
	}

	size_t new_cap = cap ? cap * SPDA_GROWTH_FACTOR : SPDA_DEFAULT_CAPACITY;
	return _spda_resize(array, new_cap);
}

SPDA_DEF void *_spda_resize(void *array, size_t size) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
		return array;
	}

	spda_header_t *header = _spda_get_header(array);

	size_t new_size;
	if (!_spda_alloc_size(size, header->stride, &new_size)) {
		SPDA_ERR(SPDA_ERR_OVERFLOW, "Requested capacity * element size exceeds SIZE_MAX.");
		return array;
	}

	spda_header_t *new_header = SPDA_REALLOC(header, new_size);
	if (new_header == NULL) {
		SPDA_ERR(SPDA_ERR_NOMEM, "Failed to reallocate the array; original left intact.");
		return array;
	}

	// Clamp only after realloc succeeds, so a failed shrink loses no elements
	new_header->capacity = size;
	if (new_header->length > size)
		new_header->length = size;

	_spda_clear_error();
	return (void *)(new_header + 1);
}

SPDA_DEF void *spda_shrink_to_fit(void *array) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
		return array;
	}

	size_t len = spda_len(array);
	size_t cap = spda_cap(array);

	// Integer-only threshold check: equivalent to len < cap * 0.25
	// avoids implicit size_t -> double promotion in the original.
	// On failure _spda_resize returns the original, which is what we want too.
	if (len > 0 && len * 4 < cap)
		return _spda_resize(array, len * SPDA_GROWTH_FACTOR);

	_spda_clear_error();   // nothing to shrink is still a success
	return array;
}

SPDA_DEF void *_spda_append(void *array, const void *value) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
		return array;
	}

	if (value == NULL) {
		SPDA_ERR(SPDA_ERR_INVALID_ARG, "Value cannot be NULL.");
		return array;
	}
	
	size_t length = spda_len(array);
	size_t stride = spda_stride(array);
	size_t capacity = spda_cap(array);
	if (length >= capacity) {
		array = _spda_resize_def(array);
		if (spda_cap(array) <= length)   // grow failed: array returned unchanged
			return array;
	}
	
	memcpy((char *)array + length * stride, value, stride);
	_spda_get_header(array)->length += 1; // increment length
	_spda_clear_error();
	return array;
}

SPDA_DEF void *_spda_append_many(void *array, const void *items, size_t item_count) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
		return array;
	}

	if (item_count == 0) {   // nothing to add is a success
		_spda_clear_error();
		return array;
	}

	if (items == NULL) {
		SPDA_ERR(SPDA_ERR_INVALID_ARG, "Items cannot be NULL when item count is non-zero.");
		return array;
	}
	
	size_t len    = spda_len(array);
	size_t cap    = spda_cap(array);
	size_t stride = spda_stride(array);

	if (item_count > SIZE_MAX - len) {
		SPDA_ERR(SPDA_ERR_OVERFLOW, "Length + item count exceeds SIZE_MAX.");
		return array;
	}
	size_t required = len + item_count;

	// `items` may point into this array (e.g. appending it to itself). The
	// resize below can move the block, so remember the source as an offset.
	// Integer comparison avoids UB from relational ops on unrelated pointers.
	uintptr_t base = (uintptr_t)array;
	uintptr_t src  = (uintptr_t)items;
	bool aliased   = src >= base && src < base + cap * stride;
	size_t offset  = aliased ? (size_t)(src - base) : 0;

	if (aliased) {
		// Source must lie within the live elements: anything past `len` is
		// uninitialised and would overlap the destination of the copy.
		size_t live = len * stride;
		if (offset > live || item_count > (live - offset) / stride) {
			SPDA_ERR(SPDA_ERR_INVALID_ARG, "Items overlap the array but extend past its length.");
			return array;
		}
	}

	// Single resize upfront if needed, instead of realloc-ing on every boundary
	if (required > cap) {
		// Grow by GROWTH_FACTOR until capacity fits required. Start from the
		// default when cap is 0 (0 * factor never grows), and jump straight to
		// `required` if another doubling would overflow.
		size_t new_cap = cap ? cap : SPDA_DEFAULT_CAPACITY;
		while (new_cap < required)
			new_cap = new_cap > SIZE_MAX / SPDA_GROWTH_FACTOR ? required
			                                                  : new_cap * SPDA_GROWTH_FACTOR;

		array = _spda_resize(array, new_cap);
		if (spda_cap(array) < required)  // resize failed: array returned unchanged
			return array;
	}

	if (aliased)
		items = (char *)array + offset;   // re-point into the (possibly moved) block

	memcpy((char *)array + len * stride, items, item_count * stride);
	_spda_get_header(array)->length = required;
	_spda_clear_error();
	return array;
}

SPDA_DEF void *_spda_insert(void *array, size_t idx, const void *value) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
		return array;
	}

	if (value == NULL) {
		SPDA_ERR(SPDA_ERR_INVALID_ARG, "Value cannot be NULL");
		return array;
	}

	size_t length = spda_len(array);
	size_t stride = spda_stride(array);
	size_t capacity = spda_cap(array);

	if (idx > length) {
		SPDA_ERR(SPDA_ERR_OUT_OF_BOUNDS, "Start index out of bounds for insert.");
		return array;
	}

	if (length >= capacity) {
		array = _spda_resize_def(array);
		if (spda_cap(array) <= length)   // grow failed: nothing inserted
			return array;
	}

	memmove((char *)array + (idx + 1) * stride, (char *)array + idx * stride,
		(length - idx) * stride);
	memcpy((char *)array + idx * stride, value, stride);
	_spda_get_header(array)->length += 1; 
	_spda_clear_error();
	return array;
}

SPDA_DEF bool _spda_remove(void *array, size_t idx, size_t count, void *dest) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
		return false;
	}

	size_t length = spda_len(array);
	size_t stride = spda_stride(array);

	if (length == 0) {
		SPDA_ERR(SPDA_ERR_OUT_OF_BOUNDS, "Cannot remove from an empty array.");
		return false;
	}

	if (idx >= length) {
		SPDA_ERR(SPDA_ERR_OUT_OF_BOUNDS, "Start index out of bounds for remove.");
		return false;
	}
	
	if (count > length - idx) {
		SPDA_ERR(SPDA_ERR_OUT_OF_BOUNDS, "Range exceeds array length for remove.");
		return false;
	} 
	
	if (count == 0) {	// Nothing to be done
		_spda_clear_error();
		return true;
	}

	if (dest) {
		memcpy(dest, (char *)array + idx * stride, count * stride);
	}

	memmove((char *)array + idx * stride, (char *)array + (idx + count) * stride,
	(length - idx - count) * stride);

	_spda_get_header(array)->length -= count;
	_spda_clear_error();
	return true;
}

SPDA_DEF bool _spda_swap_remove(void *array, size_t idx, void *dest) {
    if (!_spda_is_valid(array)) {
        SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
        return false;
    }

    size_t length = spda_len(array);
    size_t stride = spda_stride(array);

    if (length == 0) {
        SPDA_ERR(SPDA_ERR_OUT_OF_BOUNDS, "Cannot remove from an empty array.");
        return false;
    }

    if (idx >= length) {
        SPDA_ERR(SPDA_ERR_OUT_OF_BOUNDS, "Index out of bounds for swap remove");
        return false;
    }

    char *slot = (char *)array + idx * stride;
    char *last = (char *)array + (length - 1) * stride;

    if (dest) memcpy(dest, slot, stride);   // hand back the removed element
    if (slot != last) memcpy(slot, last, stride);  // fill the gap with the last one

    _spda_get_header(array)->length -= 1;
    _spda_clear_error();
    return true;
}

SPDA_DEF void spda_reverse(void *array) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
		return;
	}
	
	size_t stride = spda_stride(array);
	size_t len = spda_len(array);

	for (size_t i = 0; i < len / 2; ++i) {
		_spda_swap_bytes((char *)array + i * stride,
		                 (char *)array + (len - i - 1) * stride, stride);
	}

	_spda_clear_error();
}

SPDA_DEF bool spda_set_length(void *array, size_t length) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
		return false;
	}

	if (length > spda_cap(array)) {
		SPDA_ERR(SPDA_ERR_OUT_OF_BOUNDS, "Provided length is greater than the capacity of array.");
		return false;
	}

	_spda_get_header(array)->length = length;
	_spda_clear_error();
	return true;
}

SPDA_DEF void spda_clear(void *array) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
		return;
	}

	spda_set_length(array, 0);
}

SPDA_DEF void *spda_copy(void *src) {
	if (!_spda_is_valid(src)) {
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
		return NULL;
	}
	
	size_t capacity = spda_cap(src);
	size_t stride = spda_stride(src);
	size_t arr_size = capacity * stride;
	size_t header_size = sizeof(spda_header_t);
	
	void *_dst = _spda_create(capacity, stride);
	if (_dst == NULL)
		return NULL;   // _spda_create has recorded the reason
	
	memcpy(
		(spda_header_t *)_dst - 1, 
		(spda_header_t *) src - 1, 
		arr_size + header_size
	);
	
	_spda_clear_error();
	return (void *)_dst;
}

SPDA_DEF void spda_sort(void *array, int (*compar)(const void *, const void *)) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
		return;
	}

	if (!compar) {
		SPDA_ERR(SPDA_ERR_INVALID_ARG, "Comparator cannot be NULL.");
		return;
	}

	qsort(array, spda_len(array), spda_stride(array), compar);
	_spda_clear_error();
}

SPDA_DEF void _spda_fprint(FILE *out, const void *array, spda_elem_fn pfn) {
	if (out == NULL) {
		SPDA_ERR(SPDA_ERR_INVALID_ARG, "Output stream cannot be NULL.");
		return;
	}

	if (pfn == NULL) {
		SPDA_ERR(SPDA_ERR_INVALID_ARG, "Element printer cannot be NULL.");
		return;
	}

	if (!_spda_is_valid(array)) {
		fputs("NULL", out);   // still printable, but reported
		SPDA_ERR(SPDA_ERR_INVALID_ARRAY, "Pointer not a spda compatible dynamic array.");
		return;
	}

	size_t len = spda_len(array);
	size_t stride = spda_stride(array);
	
	fputc('[', out);
	for (size_t i = 0; i < len; ++i) {
		if (i) fputs(", ", out);
		pfn(out, (const char *)array + i * stride);
	}
	fputc(']', out);
	_spda_clear_error();
}

/* Default `spda_elem_fn` element printers */
SPDA_DEF void spda_fmt_bool   (FILE *o, const void *e) { fputs(*(const bool *)e ? "true" : "false", o); }
SPDA_DEF void spda_fmt_char   (FILE *o, const void *e) { fprintf(o, "'%c'", *(const char *)e); }
SPDA_DEF void spda_fmt_schar  (FILE *o, const void *e) { fprintf(o, "%d", *(const signed char *)e); }
SPDA_DEF void spda_fmt_uchar  (FILE *o, const void *e) { fprintf(o, "%u", *(const unsigned char *)e); }
SPDA_DEF void spda_fmt_short  (FILE *o, const void *e) { fprintf(o, "%d", *(const short *)e); }
SPDA_DEF void spda_fmt_ushort (FILE *o, const void *e) { fprintf(o, "%u", *(const unsigned short *)e); }
SPDA_DEF void spda_fmt_int    (FILE *o, const void *e) { fprintf(o, "%d", *(const int *)e); }
SPDA_DEF void spda_fmt_uint   (FILE *o, const void *e) { fprintf(o, "%u", *(const unsigned int *)e); }
SPDA_DEF void spda_fmt_long   (FILE *o, const void *e) { fprintf(o, "%ld", *(const long *)e); }
SPDA_DEF void spda_fmt_ulong  (FILE *o, const void *e) { fprintf(o, "%lu", *(const unsigned long *)e); }
SPDA_DEF void spda_fmt_llong  (FILE *o, const void *e) { fprintf(o, "%lld", *(const long long *)e); }
SPDA_DEF void spda_fmt_ullong (FILE *o, const void *e) { fprintf(o, "%llu", *(const unsigned long long *)e); }
SPDA_DEF void spda_fmt_size   (FILE *o, const void *e) { fprintf(o, "%zu", *(const size_t *)e); }
SPDA_DEF void spda_fmt_float  (FILE *o, const void *e) { fprintf(o, "%g", *(const float *)e); }
SPDA_DEF void spda_fmt_double (FILE *o, const void *e) { fprintf(o, "%g", *(const double *)e); }
SPDA_DEF void spda_fmt_ldouble(FILE *o, const void *e) { fprintf(o, "%Lg", *(const long double *)e); }
SPDA_DEF void spda_fmt_str    (FILE *o, const void *e) {
    const char *s = *(const char *const *)e;
    fprintf(o, s ? "\"%s\"" : "%s", s ? s : "NULL");
}

#endif // SPDA_IMPLEMENTATION_GUARD_