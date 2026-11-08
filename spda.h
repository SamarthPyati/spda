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

#define SPDA_GROWTH_FACTOR 1.5
#define SPDA_SHRINK_THRESHOLD 0.25 // if array utilisation falls below 25% shrink the capacity of array 

/* ERROR HANDLING */
#define SPDA_ERR(etype, msg)                                                            \
    do {                                                                                \
        fprintf(stderr, "%s:%d [%s] - %s\n", __FILE__, __LINE__, etype, msg);           \
    } while (0)                                                         

// Length for vanilla c arrays
#define CARRAY_LEN(xs) (sizeof((xs)) / sizeof((xs)[0]))

/* Core Operations */
void *_spda_create(size_t cap, size_t stride);
void _spda_destroy(void *array);

/* Field Operations */
static inline spda_header_t* _spda_get_header(const void *array) {
    if (!array) return NULL;
    return (spda_header_t *)array - 1;
}

static inline bool _spda_is_valid(const void *array) {
    if (!array) return false;
    spda_header_t *header = _spda_get_header(array);
    return header && header->stride > 0;
}

static inline size_t spda_len(const void *array) {
    if (!array) return 0;
    return _spda_get_header(array)->length;
}

static inline size_t spda_cap(const void *array) {
    if (!array) return 0;
    return _spda_get_header(array)->capacity;
}

static inline size_t spda_stride(const void *array) {
    if (!array) return 0;
    return _spda_get_header(array)->stride;
}

/* Memory Operations */
void *_spda_resize_def(void *array);
void *_spda_resize(void *array, size_t size);     
void *spda_shrink_to_fit(void *array);

/* Array Operations */
void *_spda_append(void *array, const void* value);
void *_spda_append_many(void *array, void *items, size_t item_count);
void *_spda_insert(void *array, int idx, const void* value);

void _spda_pop(void *array);
bool _spda_pop_ret(void *array, void *dest);                 // return the popped item

void *_spda_remove(void *array, int idx);
void *_spda_remove_ret(void *array, int idx, void *dest);    // return the removed item

void _spda_reverse(void *array);                             // reverse array inplace

/* Sort and search */
void spda_sort(void *array, int (*compar)(const void *, const void *));   // qsort  
void *spda_search(void *array, const void *target); // TODO                      

/* Utilities */ 
void *spda_copy(void *src);
void spda_clear(void *array);
void spda_print(void *array, void (*spdaElemPrinter)(void *elem));
void spda_print_metadata(void *array);

/* Random Helper Functions */ 
int randint(int min, int max);
float randfloat(float min, float max);

/* Random Array Generation */
void spda_rand(int **array, size_t n, int min, int max);                 // Populate Random int array 
void spda_randf(float **array, size_t n, float min, float max);          // Populate Random double array 

/* Default datatype element printer */
void _printInt(void *elem);
void _printFloat(void *elem);
void _printDouble(void *elem);
void _printChar(void *elem);
void _printStr(void *elem); 

#define printInt _printInt
#define printFloat _printFloat
#define printDouble _printDouble
#define printChar _printChar
#define printStr _printStr

/* Macros */
#define spda_create(type) \
    (type *) _spda_create(SPDA_DEFAULT_CAPACITY, sizeof(type))

#define spda_reserve(type, capacity) \
    (type *) _spda_create(capacity, sizeof(type))    

#define spda_destroy(array)  _spda_destroy(array)

#define spda_append(array, value)                     \
    do {                                              \
        __typeof__(*(array)) spda__tmp_ = (value);    \
        (array) = _spda_append((array), &spda__tmp_); \
    } while (0)

#define spda_append_many(array, ...)                                              \
    do {                                                                          \
        __typeof__(*(array)) spda__tmp_[] = {__VA_ARGS__};                        \
        (array) = _spda_append_many((array), spda__tmp_, CARRAY_LEN(spda__tmp_)); \
    } while (0)

#define spda_append_items(array, items, count)                            \
    do {                                                                  \
            (array) = _spda_append_many((array), (items), (count));       \
    } while (0)

#define spda_pop(array) _spda_pop(array)
#define spda_pop_ret(array, dest) _spda_pop_ret(array, dest)

#define spda_foreach(type, varname, array) \
    for (type *varname = (array); varname != (array) + spda_len(array); ++varname)

#define spda_insert(array, idx, value)                       \
    do {                                                     \
        __typeof__(*(array)) spda__tmp_ = (value);           \
        (array) = _spda_insert((array), (idx), &spda__tmp_); \
    } while (0)

#define spda_remove(array, idx) _spda_remove((array), idx)
#define spda_remove_ret(array, idx, dest) _spda_remove_ret((array), idx, dest)

#define spda_reverse(array) \
    _spda_reverse((array))

#define spda_clear(array) \
    _spda_get_header((array))->length = 0

#define spda_set_length(array, value) \
    _spda_get_header((array))->length = (value)

#endif // SPDA_HEADER_GUARD_H_

#ifdef SPDA_IMPLEMENTATION

void *_spda_create(size_t capacity, size_t stride) {
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

void _spda_destroy(void *array) {
  if (!_spda_is_valid(array))
    return;
  
  spda_header_t *header = _spda_get_header(array);
  free(header);
}

void *_spda_resize_def(void *array) {
  if (!_spda_is_valid(array))
    return NULL;

  spda_header_t *header = _spda_get_header(array);
  size_t new_cap = (size_t)header->capacity * SPDA_GROWTH_FACTOR;
  size_t new_size = sizeof(spda_header_t) + (new_cap * header->stride);

  spda_header_t *new_header = realloc(header, new_size);
  if (new_header == NULL) {
    SPDA_ERR("MEM_ALLOCATION", "Failed to reallocate the header and the array.");
    return NULL;
  }

  new_header->capacity = new_cap;
  return (void *)(new_header + 1);  
}

void *_spda_resize(void *array, size_t size) {
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

void *spda_shrink_to_fit(void *array) {
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

void *_spda_append(void *array, const void *value) {
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

void *_spda_append_many(void *array, void *items, size_t item_count) {
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

void _spda_pop(void *array) {
  size_t length = spda_len(array);
  if (length == 0) {
    SPDA_ERR("INDEX_OUT_OF_BOUNDS", "Cannot pop elements from an empty array");
    return;
  }
  _spda_get_header(array)->length -= 1; // decrement length
}

bool _spda_pop_ret(void *array, void *dest) {
  size_t length = spda_len(array);

  if (length == 0) {
    SPDA_ERR("INDEX_OUT_OF_BOUNDS", "Cannot pop elements from an empty array");
    return false;
  }

  if (dest) {
    size_t stride = spda_stride(array);
    memcpy(dest, (char *)array + ((length - 1) * stride), stride);
  }

  _spda_get_header(array)->length -= 1; // decrement length
  return true;
}

void *_spda_insert(void *array, int idx, const void *value) {
  size_t length = spda_len(array);
  size_t stride = spda_stride(array);
  size_t capacity = spda_cap(array);
  if (idx < 0 || (size_t)idx > length) {
    SPDA_ERR("INDEX_OUT_OF_BOUNDS", "Index out of bounds for insert");
    return array;
  }
  if (length >= capacity) {
    array = _spda_resize_def(array);
  }
  memmove((char *)array + (idx + 1) * stride, (char *)array + idx * stride,
          (length - idx) * stride);
  memcpy((char *)array + idx * stride, value, stride);
  _spda_get_header(array)->length += 1; 
  return array;
}

void *_spda_remove(void *array, int idx) {
  size_t length = spda_len(array);
  size_t stride = spda_stride(array);
  if (idx < 0 || (size_t)idx >= length) {
    SPDA_ERR("INDEX_OUT_OF_BOUNDS", "Index out of bounds for remove");
    return array;
  }
  memmove((char *)array + idx * stride, (char *)array + (idx + 1) * stride,
          (length - idx - 1) * stride);
  _spda_get_header(array)->length -= 1;
  return array;
}

void *_spda_remove_ret(void *array, int idx, void *dest) {
  size_t length = spda_len(array);
  size_t stride = spda_stride(array);
  if (idx < 0 || (size_t)idx >= length) {
    SPDA_ERR("INDEX_OUT_OF_BOUNDS", "Index out of bounds for remove");
    return array;
  }
  memcpy(dest, (char *)array + idx * stride, stride);
  memmove((char *)array + idx * stride, (char *)array + (idx + 1) * stride,
          (length - idx - 1) * stride);
  _spda_get_header(array)->length -= 1;
  return array;
}

void _spda_reverse(void *array) {
  if (!_spda_is_valid(array))
    return;

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

void *spda_copy(void *src) {
  if (src == NULL) {
    SPDA_ERR("INVALID_SOURCE", "Source array cannot be NULL");
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

void spda_sort(void *array, int (*compar)(const void *, const void *)) {
  if (!array) {
    SPDA_ERR("INVALID_SOURCE", "Source array cannot be NULL");
    exit(EXIT_FAILURE);
  }

  size_t length = spda_len(array);
  size_t stride = spda_stride(array);

  qsort(array, length, stride, compar);
}

void spda_print_metadata(void *array) {
  if (!array) {
    SPDA_ERR("INVALID_SOURCE", "Source array cannot be NULL");
    exit(EXIT_FAILURE);
  }
  /* Printing the Metadata of the array */
  printf("Capacity: %zu, Length: %zu, Stride (bits): %zu\n", spda_cap(array),
         spda_len(array), spda_stride(array) * 8);
}

void spda_print(void *array, void (*spdaElemPrinter)(void *elem)) {
  for (size_t i = 0; i < spda_len(array); ++i) {
    void *p = ((char *)array + i * spda_stride(array));
    spdaElemPrinter(p);
  }
  printf("\n");
}

/* Default `spdaElemPrinter` functions for convenience */
void _printInt(void *elem) { printf("%d ", *(int *)elem); }

void _printFloat(void *elem) { printf("%f ", *(float *)elem); }

void _printDouble(void *elem) { printf("%lf ", *(double *)elem); }

void _printChar(void *elem) { printf("%c ", *(char *)elem); }

void _printStr(void *elem) { printf("%s\n", *(const char **)elem); }

/* Random Function Utilities */
int randint(int min, int max) { return rand() % (max - min + 1) + min; }

float randfloat(float min, float max) {
  float f;
  f = ((float)rand() / ((float)RAND_MAX + 1));
  return (min + f * (max - min));
}

/* Generating random arrays */
void spda_rand(int **array, size_t n, int min, int max) {
  for (size_t i = 0; i < n; ++i) {
    int random_value = randint(min, max);
    *array = _spda_append(*array, &random_value);
  }
}

void spda_randf(float **array, size_t n, float min, float max) {
  for (size_t i = 0; i < n; ++i) {
    float random_value = randfloat(min, max);
    *array = _spda_append(*array, &random_value);
  }
}

#endif // SPDA_IMPLEMENTATION