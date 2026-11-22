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
*  +---------------------------------------+---------------------------------------+
*  |  capacity   |  length   |  stride     |               elements                |
*  +---------------------------------------+---------------------------------------+
*  |  (size_t)   |  (size_t) |  (size_t)   |               (void *)                |
*  +---------------------------------------+---------------------------------------+
*                                          ^
*                                      array pointer 
*/

#ifndef SPDA_HEADER_GUARD_H_
#define SPDA_HEADER_GUARD_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

typedef struct {
    size_t capacity; 
    size_t length;
    size_t stride;
} spda_header_t;

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
#define SPDA_SHRINK_THRESHOLD 0.25 // if array utilisation falls below 25% shrink the capacity of array 

/* ERROR HANDLING */
#define SPDA_ERR(etype, msg)                                                            \
    do {                                                                                \
        fprintf(stderr, "%s:%d [%s] - %s\n", __FILE__, __LINE__, etype, msg);           \
    } while (0)     

#define SPDA_COUNTOF(xs) (sizeof((xs)) / sizeof((xs)[0]))

/* Core Operations */
SPDA_DEF void *_spda_create(size_t cap, size_t stride);
SPDA_DEF void spda_destroy(void *array);

/* Field Operations */
static inline spda_header_t* _spda_get_header(const void *array) {
    return array ? (spda_header_t *)array - 1 : NULL;
}

static inline size_t spda_len(const void *array) {
    return array ? _spda_get_header(array)->length : 0;
}

static inline size_t spda_cap(const void *array) {
    return array ? _spda_get_header(array)->capacity : 0;
}

static inline size_t spda_stride(const void *array) {
    return array ? _spda_get_header(array)->stride : 0;
}

static inline bool _spda_is_valid(const void *array) {
	return spda_stride(array) > 0;
}

/* Memory Operations */
SPDA_DEF void *_spda_resize_def(void *array);
SPDA_DEF void *_spda_resize(void *array, size_t size);
SPDA_DEF void *spda_shrink_to_fit(void *array);

/* Array Operations */
SPDA_DEF void *_spda_append(void *array, const void* value);
SPDA_DEF void *_spda_append_many(void *array, void *items, size_t item_count);
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

SPDA_DEF void *_spda_create(size_t capacity, size_t stride) {
	if (capacity < SPDA_DEFAULT_CAPACITY)
	capacity = SPDA_DEFAULT_CAPACITY;
	
	if (stride == 0) {
		SPDA_ERR("INVALID_ARGUMENT", "Stride (size of datatype) cannot be zero");
		return NULL;
	}
	
	size_t header_size = sizeof(spda_header_t);
	size_t array_size = capacity * stride;
	spda_header_t *array = (spda_header_t *)malloc(header_size + array_size);
	
	if (!array) {
		SPDA_ERR("MEM_ALLOCATION", "Failed memory allocation for dynamic array.");
		return NULL;
	}
	
	array->capacity = capacity;
	array->length = 0;
	array->stride = stride;
	return (void *)(array + 1);
}

SPDA_DEF void spda_destroy(void *array) {
	if (!_spda_is_valid(array))
		return;
	
	spda_header_t *header = _spda_get_header(array);
	free(header);
}

SPDA_DEF void *_spda_resize_def(void *array) {
	if (!_spda_is_valid(array))
		return NULL;
	
	spda_header_t *header = _spda_get_header(array);
	size_t new_cap = header->capacity ? (size_t)header->capacity * SPDA_GROWTH_FACTOR
	                                  : SPDA_DEFAULT_CAPACITY;
	size_t new_size = sizeof(spda_header_t) + (new_cap * header->stride);
	
	spda_header_t *new_header = realloc(header, new_size);
	if (new_header == NULL) {
		SPDA_ERR("MEM_ALLOCATION", "Failed to reallocate the header and the array.");
		return NULL;
	}
	
	new_header->capacity = new_cap;
	return (void *)(new_header + 1);  
}

SPDA_DEF void *_spda_resize(void *array, size_t size) {
	if (!_spda_is_valid(array))
		return NULL;

	spda_header_t *header = _spda_get_header(array);
	
	if (header->length > size)  {
		// Clamp the length to the new capacity
		header->length = size;
	}
	
	size_t new_size = sizeof(spda_header_t) + (size * header->stride);
	
	spda_header_t *new_header = realloc(header, new_size);
	
	if (new_header == NULL) {
		SPDA_ERR("MEM_ALLOCATION", "Failed to reallocate the array header.");
		return NULL;
	}
	
	new_header->capacity = size;
	return (void *)(new_header + 1);
}

SPDA_DEF void *spda_shrink_to_fit(void *array) {
	if (!_spda_is_valid(array))
		return NULL;
	
	size_t len = spda_len(array);
	size_t cap = spda_cap(array);
	
	// Integer-only threshold check: equivalent to len < cap * 0.25
	// avoids implicit size_t -> double promotion in the original
	if (len > 0 && len * 4 < cap) {
		void *new_array = _spda_resize(array, len * SPDA_GROWTH_FACTOR);
		if (new_array == NULL) {
			SPDA_ERR("MEM_ALLOCATION", "Failed to shrink array");
			return array;  // return original on failure, not NULL
		}
		return new_array;
	}
	return array;
}

SPDA_DEF void *_spda_append(void *array, const void *value) {
	if (!_spda_is_valid(array) || !value) {
		SPDA_ERR("INVALID_ARGUMENT", "Invalid array or value");
		return array;
	}
	
	size_t length = spda_len(array);
	size_t stride = spda_stride(array);
	size_t capacity = spda_cap(array);
	if (length >= capacity) {
		void *new_array = _spda_resize_def(array);
		if (new_array == NULL) {
			SPDA_ERR("MEM_ALLOCATION", "Failed to resize array");
			return array; // Return original array if resize fails
		}
		array = new_array;
	}
	
	memcpy((char *)array + length * stride, value, stride);
	_spda_get_header(array)->length += 1; // increment length
	return array;
}

SPDA_DEF void *_spda_append_many(void *array, void *items, size_t item_count) {
	if (!_spda_is_valid(array) || !items || item_count == 0)
		return array;
	
	size_t len    = spda_len(array);
	size_t cap    = spda_cap(array);
	size_t stride = spda_stride(array);
	size_t required = len + item_count;
	
	// Single resize upfront if needed, instead of realloc-ing on every boundary
	if (required > cap) {
		// Grow by GROWTH_FACTOR until capacity fits required
		size_t new_cap = cap;
		while (new_cap < required)
		new_cap *= SPDA_GROWTH_FACTOR;
		
		void *resized = _spda_resize(array, new_cap);
		if (resized == NULL) {
			SPDA_ERR("MEM_ALLOCATION", "Failed to resize array in append_many");
			return array;
		}
		array = resized;
	}
	
	// One memcpy for the entire block instead of per-element copies
	memcpy((char *)array + len * stride, items, item_count * stride);
	_spda_get_header(array)->length = required;
	
	return array;
}

SPDA_DEF void *_spda_insert(void *array, size_t idx, const void *value) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR("INVALID_ARRAY", "Pointer not a spda compatible dynamic array.");
		return NULL;
	}

	if (value == NULL) {
		SPDA_ERR("INVALID_ARGUMENT", "Value cannot be NULL");
		return array;
	}

	size_t length = spda_len(array);
	size_t stride = spda_stride(array);
	size_t capacity = spda_cap(array);

	if (idx > length) {
		SPDA_ERR("INDEX_OUT_OF_BOUNDS", "Start index out of bounds for insert.");
		return array;
	}

	if (length >= capacity) {
		void *tmp = _spda_resize_def(array);
		if (tmp == NULL) {
			SPDA_ERR("MEM_ALLOCATION", "Failed to resize array in insert");
			return array; // original is still valid; nothing inserted
		}
		array = tmp;
	}

	memmove((char *)array + (idx + 1) * stride, (char *)array + idx * stride,
		(length - idx) * stride);
	memcpy((char *)array + idx * stride, value, stride);
	_spda_get_header(array)->length += 1; 
	return array;
}

SPDA_DEF bool _spda_remove(void *array, size_t idx, size_t count, void *dest) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR("INVALID_ARRAY", "Pointer not a spda compatible dynamic array.");
		return false;
	}

	size_t length = spda_len(array);
	size_t stride = spda_stride(array);

	if (idx >= length) {
		SPDA_ERR("INDEX_OUT_OF_BOUNDS", "Start index out of bounds for remove.");
		return false;
	}
	
	if (count > length - idx) {
		SPDA_ERR("INDEX_OUT_OF_BOUNDS", "Range exceeds array length for remove.");
		return false;
	} 
	
	if (count == 0) return true;	// Nothing to be done

	if (dest) {
		memcpy(dest, (char *)array + idx * stride, count * stride);
	}

	memmove((char *)array + idx * stride, (char *)array + (idx + count) * stride,
	(length - idx - count) * stride);

	_spda_get_header(array)->length -= count;
	return true;
}

SPDA_DEF bool _spda_swap_remove(void *array, size_t idx, void *dest) {
    if (!_spda_is_valid(array)) {
        SPDA_ERR("INVALID_ARRAY", "Pointer not a spda compatible dynamic array.");
        return false;
    }

    size_t length = spda_len(array);
    size_t stride = spda_stride(array);

    if (idx >= length) {
        SPDA_ERR("INDEX_OUT_OF_BOUNDS", "Index out of bounds for swap remove");
        return false;
    }

    char *slot = (char *)array + idx * stride;
    char *last = (char *)array + (length - 1) * stride;

    if (dest) memcpy(dest, slot, stride);   // hand back the removed element
    if (slot != last) memcpy(slot, last, stride);  // fill the gap with the last one

    _spda_get_header(array)->length -= 1;
    return true;
}

SPDA_DEF void spda_reverse(void *array) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR("INVALID_ARRAY", "Pointer not a spda compatible dynamic array.");
		return;
	}
	
	size_t stride = spda_stride(array);
	size_t len = spda_len(array);
	char *temp = malloc(stride);
	
	if (!temp) {
		SPDA_ERR("MEM_ALLOCATION", "Failed to allocate temporary buffer");
		return;
	}
	
	for (size_t i = 0; i < len / 2; ++i) {
		char *a = (char *)array + i * stride;
		char *b = (char *)array + ((len - i - 1) * stride);
		memcpy(temp, a, stride);
		memcpy(a, b, stride);
		memcpy(b, temp, stride);
	}
	free(temp);
}

SPDA_DEF bool spda_set_length(void *array, size_t length) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR("INVALID_ARRAY", "Pointer not a spda compatible dynamic array.");
		return false;
	}

	if (length > spda_cap(array)) {
		SPDA_ERR("INDEX_OUT_OF_BOUNDS", "Provided length is greater than the capacity of array.");
		return false;
	}

	_spda_get_header(array)->length = length;
	return true;
}

SPDA_DEF void spda_clear(void *array) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR("INVALID_ARRAY", "Pointer not a spda compatible dynamic array.");
		return;
	}

	spda_set_length(array, 0);
}

SPDA_DEF void *spda_copy(void *src) {
	if (!_spda_is_valid(src)) {
		SPDA_ERR("INVALID_ARRAY", "Pointer not a spda compatible dynamic array.");
		return NULL;
	}
	
	size_t capacity = spda_cap(src);
	size_t stride = spda_stride(src);
	size_t arr_size = capacity * stride;
	size_t header_size = sizeof(spda_header_t);
	
	void *_dst = _spda_create(capacity, stride);
	if (_dst == NULL) {
		SPDA_ERR("MEM_ALLOCATION", "Failed to allocate memory for the new array");
		return NULL;
	}
	
	memcpy(
		(spda_header_t *)_dst - 1, 
		(spda_header_t *) src - 1, 
		arr_size + header_size
	);
	
	return (void *)_dst;
}

SPDA_DEF void spda_sort(void *array, int (*compar)(const void *, const void *)) {
	if (!_spda_is_valid(array)) {
		SPDA_ERR("INVALID_ARRAY", "Pointer not a spda compatible dynamic array.");
		return;
	}

	if (!compar) {
		SPDA_ERR("INVALID_ARGUMENT", "Comparator cannot be NULL.");
		return;
	}

	qsort(array, spda_len(array), spda_stride(array), compar);
}

SPDA_DEF void _spda_fprint(FILE *out, const void *array, spda_elem_fn pfn) {
	if (!out || !pfn) return;

	if (!_spda_is_valid(array)) {
		fputs("NULL", out); 
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