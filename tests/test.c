#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "../spda.h"

#define RED         "\x1B[31m"
#define GREEN       "\x1B[32m"
#define YELLOW      "\x1B[33m"
#define BLUE        "\x1B[34m"
#define MAGENTA     "\x1B[35m"
#define CYAN        "\x1B[36m"
#define WHITE       "\x1B[37m"
#define RESET       "\x1B[0m"

// Helper function to compare doubles with a small epsilon for floating-point precision
int double_equals(double a, double b) {
    return fabs(a - b) < 1e-9;
}

// Helper function for integer comparison
int comparInt(const void *a, const void *b) {
    return *(int *)a - *(int *)b;
}

// Helper macro for test results with error messages
#define TEST_ASSERT(cond, pass_msg, fail_msg) do { \
    if (!(cond)) { \
        printf(RED"Test failed: "RESET"%s\n", fail_msg); \
        assert(cond); \
    } else { \
        printf(GREEN"Test passed: "RESET"%s\n", pass_msg); \
    } \
} while (0)

// Operation functions for double array
void test_create() {
    printf("\nTesting array creation...\n");
    double *array = spda_create(double);
    TEST_ASSERT(spda_len(array) == 0, 
                "Initial length is zero", 
                "Expected initial length to be zero, but it was not");
    TEST_ASSERT(spda_cap(array) == SPDA_DEFAULT_CAPACITY, 
                "Initial capacity is correct", 
                "Expected initial capacity to match default capacity");
    TEST_ASSERT(spda_stride(array) == sizeof(double), 
                "Stride matches sizeof(double)", 
                "Stride did not match sizeof(double)");
    spda_destroy(array);
}

void test_append() {
    printf("\nTesting append operation...\n");
    double *array = spda_create(double);
    for (int i = 0; i < 10; i++) {
        spda_append(array, i + 0.5);
    }
    TEST_ASSERT(spda_len(array) == 10, 
                "Length after append is correct", 
                "Expected length to be 10 after append");
    TEST_ASSERT(spda_cap(array) >= 10, 
                "Capacity adjusts correctly", 
                "Capacity did not adjust correctly after append");
    spda_destroy(array);
}

void test_insert() {
    printf("\nTesting insert operation...\n");
    double *array = spda_create(double);
    for (int i = 0; i < 10; i++) {
        spda_append(array, i + 0.5);
    }
    spda_insert(array, 5, 100.75);
    TEST_ASSERT(spda_len(array) == 11, 
                "Length after insert is correct", 
                "Expected length to be 11 after insert");
    TEST_ASSERT(double_equals(array[5], 100.75), 
                "Inserted value is correct", 
                "Inserted value did not match expected value");
    spda_destroy(array);
}

void test_remove() {
    printf("\nTesting remove operation...\n");
    double *array = spda_create(double);
    for (int i = 0; i < 10; i++) {
        spda_append(array, i + 0.5);
    }
    double removed;
    spda_remove_ret(array, 5, &removed);
    TEST_ASSERT(spda_len(array) == 9, 
                "Length after remove is correct", 
                "Expected length to be 9 after remove");
    TEST_ASSERT(double_equals(removed, 5.5), 
                "Removed value is correct", 
                "Removed value did not match expected value");
    spda_destroy(array);
}

void test_reverse() {
    printf("\nTesting reverse operation...\n");
    double *array = spda_create(double);
    for (int i = 0; i < 5; i++) {
        spda_append(array, i + 0.5);
    }
    spda_reverse(array);
    bool success = true;
    for (int i = 0; i < 5; i++) {
        if (!double_equals(array[i], 4.5 - i)) {
            success = false;
        }
    }
    TEST_ASSERT(success, 
                "Reversed array contents are correct", 
                "Array contents after reverse did not match expected");
    spda_destroy(array);
}

void test_pop() {
    printf("\nTesting pop operation...\n");
    double *array = spda_create(double);
    for (int i = 0; i < 5; i++) {
        spda_append(array, i + 0.5);
    }
    double popped;
    _spda_pop_ret(array, &popped);
    TEST_ASSERT(spda_len(array) == 4, 
                "Length after pop is correct", 
                "Expected length to be 4 after pop");
    TEST_ASSERT(double_equals(popped, 4.5), 
                "Popped value is correct", 
                "Popped value did not match expected value");
    spda_destroy(array);
}

void test_clear() {
    printf("\nTesting clear operation...\n");
    double *array = spda_create(double);
    for (int i = 0; i < 5; i++) {
        spda_append(array, i + 0.5);
    }
    spda_clear(array);
    TEST_ASSERT(spda_len(array) == 0, 
                "Length after clear is zero", 
                "Expected length to be zero after clear");
    spda_destroy(array);
}

void test_resize() {
    printf("\nTesting resize operation...\n");
    double *array = spda_create(double);
    size_t old_cap = spda_cap(array);
    array = _spda_resize_def(array);
    TEST_ASSERT(spda_cap(array) == old_cap * SPDA_GROWTH_FACTOR, 
                "Capacity after resize is correct", 
                "Capacity did not grow correctly after resize");
    spda_destroy(array);
}

void test_sort_integer() {
    printf("\nTesting sort operation...\n");
    int *array = spda_create(int);
    spda_append_many(array, 3, 1, 4, 1, 5, 9, 2, 6, 5, 3);
    spda_sort(array, comparInt);
    bool success = true;
    for (size_t i = 1; i < spda_len(array); i++) {
        if (!(array[i - 1] <= array[i])) {
            success = false;
        }
    }
    TEST_ASSERT(success, 
                "Array is sorted correctly", 
                "Array contents were not sorted correctly");
    spda_destroy(array);
}

void test_shrink() {
    printf("\nTesting shrink operation...\n");

    int *array = spda_create(int);

    // Append a large number of elements
    for (size_t i = 0; i < 100; i++) {
        spda_append(array, i);
    }

    size_t original_cap = spda_cap(array);
    TEST_ASSERT(original_cap >= 100, 
                "Initial capacity is sufficient", 
                "Initial capacity is less than expected");

    // Remove elements to make the array sparse
    for (size_t i = 0; i < 80; i++) {
        spda_pop(array);
    }

    TEST_ASSERT(spda_len(array) == 20, 
                "Array length after pop is correct", 
                "Array length did not match expected value after pop");

    // Shrink the array to fit its current size
    array = spda_shrink_to_fit(array);
    size_t shrunk_cap = spda_cap(array);

    TEST_ASSERT(shrunk_cap >= spda_len(array) && shrunk_cap < original_cap, 
                "Array capacity is reduced after shrinking", 
                "Capacity was not reduced after shrinking");

    TEST_ASSERT(spda_len(array) == 20, 
                "Array length remains unchanged after shrinking", 
                "Array length changed unexpectedly after shrinking");

    spda_destroy(array);
}

void test_foreach() {
    printf("\nTesting spda_foreach macro...\n");
    int *array = spda_create(int);
    spda_append_many(array, 1, 2, 3, 4, 5);

    int sum = 0;
    spda_foreach(int, array, item) {
        sum += item;
        printf("Items: %d\n", item);
    }

    TEST_ASSERT(sum == 15,
                "spda_foreach iterates correctly and computes the sum",
                "spda_foreach failed to iterate correctly or compute the sum");

    spda_destroy(array);
}


// Main test suite
int main(void) {
    test_create();
    test_append();
    test_insert();
    test_remove();
    test_reverse();
    test_pop();
    test_clear();
    test_resize();
    test_sort_integer();
    test_shrink();
    test_foreach();

    printf(GREEN"\nAll tests passed successfully!\n"RESET);
    return 0;
}
