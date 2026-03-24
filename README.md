# spda

A **simple and generic implementation of a dynamic array** in C, designed for ease of use and high performance.

## Memory Layout

```
            ARRAY METADATA                               ELEMENTS
 +---------------------------------------+---------------------------------------+
 |  capacity   |  length   |  stride     |               elements                |
 +---------------------------------------+---------------------------------------+
 |  (size_t)   |  (size_t) |  (size_t)   |               (void *)                |
 +---------------------------------------+---------------------------------------+
                                         ^
                                    array pointer 
```

## Features
- **Generic**: Works with any data type.
- **Resizable**: Automatically resizes as elements are added.
- **Easy-to-use API**: Intuitive functions for common operations.
- **Memory-efficient**: Optimized for low memory overhead.

## Installation

#### Method 1: Build from Source
1. **Clone the repository:**
   ```sh
   git clone https://github.com/SamarthPyati/spda
   cd spda
   ```

2. **Build the dynamic library:**
   ```sh
   make build_lib
   ```

3. **Include the header file and the dynamic library (.so) in your project:**
   - Include `spda.h` in your source files.
   - Link the dynamic library using `-lspda`.
   ```sh 
    gcc -o my_program my_program.c -Lbuild -lspda -lm
   ```

#### Method 2: Manual Integration 
1. **With source files:**
    - Copy `spda.h` and `spda.c` into your project directory.
    - Include `spda.h` in your source files and compile `spda.c` along with your project:
    ```sh 
    gcc -o my_program my_program.c spda.c -lm
    ```

2. **With dynamic library:**
    - Copy `spda.h` and `build/libspda.so` into your project directory.
    - Compile by linking the library: 
    ```sh 
    gcc -o my_program my_program.c -Lbuild -lspda -lm
    ```


## Usage

### Basic Example

```c
#include <stdio.h>
#include "spda.h"

int main(void) {
    srand(time(NULL));  // Set random seed
    
    int items[10] = {3, 48, 1, 34, 0, 12, 3, 4, -1, -100};

    // Create a dynamic array and print its metadata
    int *a = spda_create(int);
    spda_print_metadata(a);

    // Append items to the array
    spda_append_items(a, items, CARRAY_LEN(items));
    spda_print(a, printInt);

    // Append multiple elements using variadic function
    spda_append_many(a, 0, 0, 0, 0);
    spda_print(a, printInt);

    // Pop elements and print the array
    for (int i = 0; i < 3; ++i) spda_pop(a);
    spda_print(a, printInt);

    // Remove elements and print the array
    for (int i = 0; i < 3; ++i) spda_remove(a, 0);
    spda_print(a, printInt);

    // Insert elements and print the array
    for (int i = 0; i < 3; ++i) spda_insert(a, 3, 1000);
    spda_print(a, printInt);

    // Iterate using spda_foreach macro
    spda_foreach(int, item, a) {
        printf("%d ", item *= 2);
    }
    printf("\n");

    // Clear and destroy the array
    spda_clear(a);
    spda_print(a, printInt);
    spda_destroy(a);

    return 0;
}
```

### Complex Example

```c
#include "spda.h"
#include <stdio.h>

typedef struct {
    double x, y, z;
} Point3D;

void printPoint3D(void *p) {
    printf("Point3D(%.1f, %.1f, %.1f)\n", 
           ((Point3D *)p)->x, ((Point3D *)p)->y, ((Point3D *)p)->z);
}

int main() {
    Point3D *points = spda_create(Point3D);
    
    Point3D p1 = {1.0, 2.0, 3.0};
    Point3D p2 = {4.0, 5.0, 6.0};
    Point3D p3 = {7.0, 8.0, 10.0};

    spda_append_many(points, p1, p2, p3);

    printf("Array contents:\n");
    spda_print(points, printPoint3D);

    spda_destroy(points);
    return 0;
}
```

## Testing

To build and run the tests:

```sh
make all
./bin/basic_test
./bin/main_test
```

## API Reference

### Creation and Destruction

- `spda_create(type)`: Create a new dynamic array for the specified type.
- `spda_reserve(type, capacity)`: Create a new dynamic array with a specified initial capacity.
- `spda_destroy(array)`: Destroy the dynamic array and free its memory.

### Element Manipulation

- `spda_append(array, value)`: Append a value to the end of the array.
- `spda_append_many(array, ...)`: Append multiple values to the array using variadic arguments.
- `spda_append_items(array, items, count)`: Append a specified number of items from a given array.
- `spda_pop(array)`: Remove the last element from the array.
- `spda_pop_ret(array)`: Remove the last element and return its value.
- `spda_insert(array, idx, value)`: Insert a value at a specified index.
- `spda_remove(array, idx)`: Remove the element at a specified index.
- `spda_remove_ret(array, idx, dest)`: Remove the element at a specified index and store its value in `dest`.

### Information and Metadata

- `spda_len(array)`: Get the current number of elements in the array.
- `spda_cap(array)`: Get the current capacity of the array.
- `spda_stride(array)`: Get the size of each element in the array.

### Utility Functions

- `spda_print(array, spdaElemPrinter)`: Print the contents of the array using a custom printer function.
- `spda_print_metadata(array)`: Print metadata such as capacity, length, and stride.

## Iteration

- `spda_foreach(type, varname, array)`: Iterate over each element in the array, with `varname` being the loop variable.

### Example:
```c
int sum = 0;
spda_foreach(int, item, array) {
    sum += item;
}
```

## License

This project is licensed under the [MIT License](LICENSE).

## Acknowledgments
- Inspired by the dynamic array implementation of [Travis Vroman's](https://github.com/travisvroman/kohi/blob/main/kohi.core/src/containers/darray.h) game engine.

