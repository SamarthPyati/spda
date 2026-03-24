#include "spda.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void *_spda_create(size_t capacity, size_t stride) {
  if (capacity < SPDA_DEFAULT_CAPACITY)
    capacity = SPDA_DEFAULT_CAPACITY;

  if (stride == 0) {
    raise("INVALID_ARGUMENT", "Stride (size of datatype) cannot be zero");
    return NULL;
  }

  size_t header_size = sizeof(spda_header_t);
  size_t array_size = capacity * stride;
  spda_header_t *array = (spda_header_t *)malloc(header_size + array_size);

  if (!array) {
    raise("MEM_ALLOCATION", "Failed memory allocation for dynamic array.");
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
  if (header == NULL) {
    raise("MEM_ALLOCATION", "Array header was not allocated properly.");
    return NULL;
  }

  spda_header_t *new_header = realloc(header, new_size);
  if (new_header == NULL) {
    raise("MEM_ALLOCATION", "Failed to reallocate the header and the array.");
    return NULL;
  }

  header = new_header;
  header->capacity = new_cap;
  return (void *)(header + 1);
}

void *_spda_resize(void *array, size_t size) {
  if (!_spda_is_valid(array))
    return NULL;

  spda_header_t *header = _spda_get_header(array);
  size_t new_cap = size;
  size_t new_size = sizeof(spda_header_t) + (new_cap * header->stride);
  if (header == NULL) {
    raise("MEM_ALLOCATION", "Array header was not allocated properly.");
    return NULL;
  }

  void *new_header = realloc(header, new_size);
  if (new_header == NULL) {
    raise("MEM_ALLOCATION", "Failed to reallocate the array header.");
    return NULL;
  }
  header = new_header;

  header->capacity = new_cap;
  return (void *)(header + 1);
}

void *spda_shrink_to_fit(void *array) {
  /* Array shrink to fit for efficient memory utilization */
  if (!_spda_is_valid(array))
    return NULL;

  size_t len = spda_len(array);
  size_t cap = spda_cap(array);

  if (len > 0 && len < cap * SPDA_SHRINK_THRESHOLD) {
    size_t new_cap = (len * 2 > SPDA_DEFAULT_CAPACITY) ? len * 2 : SPDA_DEFAULT_CAPACITY;
    array = _spda_resize(array, new_cap);
  }
  return array;
}

void *_spda_append(void *array, const void *value) {
  if (!_spda_is_valid(array) || !value) {
    raise("INVALID_ARGUMENT", "Invalid array or value");
    return array;
  }

  size_t length = spda_len(array);
  size_t stride = spda_stride(array);
  size_t capacity = spda_cap(array);
  if (length >= capacity) {
    void *new_array = _spda_resize_def(array);
    if (new_array == NULL) {
      raise("MEM_ALLOCATION", "Failed to resize array");
      return array; // Return original array if resize fails
    }
    array = new_array;
  }

  memcpy((char *)array + length * stride, value, stride);
  _spda_get_header(array)->length += 1; // increment length
  return array;
}

void *_spda_append_many(void *array, void *items, size_t item_count) {
  size_t stride = spda_stride(array);
  char *item_ptr = (char *)items;
  for (size_t i = 0; i < item_count; ++i) {
    array = _spda_append(array, item_ptr);
    item_ptr += stride;
  }
  return array;
}

void _spda_pop(void *array) {
  size_t length = spda_len(array);
  if (length == 0) {
    raise("INDEX_OUT_OF_BOUNDS", "Cannot pop elements from an empty array");
    return;
  }
  _spda_get_header(array)->length -= 1; // decrement length
}

bool _spda_pop_ret(void *array, void *dest) {
  size_t length = spda_len(array);

  if (length == 0) {
    raise("INDEX_OUT_OF_BOUNDS", "Cannot pop elements from an empty array");
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
    raise("INDEX_OUT_OF_BOUNDS", "Index out of bounds for insert");
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
    raise("INDEX_OUT_OF_BOUNDS", "Index out of bounds for remove");
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
    raise("INDEX_OUT_OF_BOUNDS", "Index out of bounds for remove");
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
    raise("MEM_ALLOCATION", "Failed to allocate temporary buffer");
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
    raise("INVALID_SOURCE", "Source array cannot be NULL");
    return NULL;
  }

  size_t capacity = spda_cap(src);
  size_t length = spda_len(src);
  size_t stride = spda_stride(src);
  size_t arr_size = capacity * stride;
  size_t header_size = sizeof(spda_header_t);

  void *_dst = _spda_create(capacity, stride);
  if (_dst == NULL) {
    raise("MEM_ALLOCATION", "Failed to allocate memory for the new array");
    return NULL;
  }
  memcpy(_dst, (spda_header_t *)src - 1, arr_size + header_size);
  _spda_get_header(_dst)->length = length;
  return (void *)((spda_header_t *)_dst + 1);
}

void spda_sort(void *array, int (*compar)(const void *, const void *)) {
  if (!array) {
    raise("INVALID_SOURCE", "Source array cannot be NULL");
    exit(EXIT_FAILURE);
  }

  size_t length = spda_len(array);
  size_t stride = spda_stride(array);

  qsort(array, length, stride, compar);
}

void spda_print_metadata(void *array) {
  if (!array) {
    raise("INVALID_SOURCE", "Source array cannot be NULL");
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
