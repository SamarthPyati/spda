#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "../spda.h"

#define RED     "\x1B[31m"
#define GREEN   "\x1B[32m"
#define RESET   "\x1B[0m"

int double_equals(double a, double b) {
    return fabs(a - b) < 1e-9;
}

int comparInt(const void *a, const void *b) {
    return *(int *)a - *(int *)b;
}

#define TEST_ASSERT(cond, pass_msg, fail_msg) do { \
    if (!(cond)) { \
        printf(RED "Test failed: " RESET "%s\n", fail_msg); \
        assert(cond); \
    } else { \
        printf(GREEN "Test passed: " RESET "%s\n", pass_msg); \
    } \
} while (0)

void test_create() {
    printf("\nTesting array creation...\n");
    double *array = spda_create(double);
    TEST_ASSERT(spda_len(array) == 0,
                "Initial length is zero",
                "Expected initial length to be zero");
    TEST_ASSERT(spda_cap(array) == SPDA_DEFAULT_CAPACITY,
                "Initial capacity matches default",
                "Expected initial capacity to match SPDA_DEFAULT_CAPACITY");
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
                "Length after 10 appends is correct",
                "Expected length to be 10 after append");
    TEST_ASSERT(spda_cap(array) >= 10,
                "Capacity is at least 10 after appends",
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
                "Inserted value is at correct index",
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
    // FIX: use public macro instead of calling _spda_pop_ret directly
    spda_pop_ret(array, &popped);
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
                "Capacity doubled after default resize",
                "Capacity did not grow by SPDA_GROWTH_FACTOR after resize");
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
                "Array is sorted in ascending order",
                "Array contents were not sorted correctly");
    spda_destroy(array);
}

void test_shrink() {
    printf("\nTesting shrink operation...\n");
    int *array = spda_create(int);

    for (size_t i = 0; i < 100; i++) {
        spda_append(array, (int)i);
    }

    size_t original_cap = spda_cap(array);
    TEST_ASSERT(original_cap >= 100,
                "Capacity is sufficient for 100 elements",
                "Initial capacity is less than expected");

    for (size_t i = 0; i < 80; i++) {
        spda_pop(array);
    }

    TEST_ASSERT(spda_len(array) == 20,
                "Length is 20 after 80 pops",
                "Array length did not match expected value after pop");

    array = spda_shrink_to_fit(array);
    size_t shrunk_cap = spda_cap(array);

    TEST_ASSERT(shrunk_cap >= spda_len(array),
                "Shrunk capacity still fits all elements",
                "Shrunk capacity is smaller than current length");
    TEST_ASSERT(shrunk_cap < original_cap || shrunk_cap == SPDA_DEFAULT_CAPACITY,
                "Capacity is reduced after shrink_to_fit",
                "Capacity was not reduced after shrinking");
    TEST_ASSERT(spda_len(array) == 20,
                "Length unchanged after shrink_to_fit",
                "Array length changed unexpectedly after shrinking");

    spda_destroy(array);
}

void test_foreach() {
    printf("\nTesting spda_foreach macro...\n");
    int *array = spda_create(int);
    spda_append_many(array, 1, 2, 3, 4, 5);

    int sum = 0;
    spda_foreach(int, item, array) {
        sum += *item;
        // printf("Item: %d\n", *item);
    }

    TEST_ASSERT(sum == 15,
                "spda_foreach sums elements correctly",
                "spda_foreach failed to iterate correctly or compute the sum");

    // Also verify in-place mutation works with the pointer-based macro
    spda_foreach(int, item, array) {
        *item *= 2;
    }
    TEST_ASSERT(array[0] == 2 && array[4] == 10,
                "spda_foreach in-place mutation works correctly",
                "spda_foreach mutation did not modify the array");

    spda_destroy(array);
}

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

    printf(GREEN "\nAll tests passed successfully!\n" RESET);
    return 0;
}