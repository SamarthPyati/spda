#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../spda.h"

#define RED     "\x1B[31m"
#define GREEN   "\x1B[32m"
#define YELLOW  "\x1B[33m"
#define RESET   "\x1B[0m"

typedef struct {
    double x, y, z;
} Point3D;

typedef struct {
    int id;
    char name[32];
} Person;

int double_equals(double a, double b) {
    return fabs(a - b) < 1e-9;
}

int float_equals(float a, float b) {
    return fabsf(a - b) < 1e-6f;
}

int comparInt(const void *a, const void *b) {
    return *(int *)a - *(int *)b;
}

int comparFloat(const void *a, const void *b) {
    float fa = *(float *)a, fb = *(float *)b;
    return (fa > fb) - (fa < fb);
}

int comparDouble(const void *a, const void *b) {
    double da = *(double *)a, db = *(double *)b;
    return (da > db) - (da < db);
}

int comparChar(const void *a, const void *b) {
    return *(char *)a - *(char *)b;
}

int comparPoint3D(const void *a, const void *b) {
    Point3D *pa = (Point3D *)a, *pb = (Point3D *)b;
    if (pa->x != pb->x) return (pa->x > pb->x) - (pa->x < pb->x);
    if (pa->y != pb->y) return (pa->y > pb->y) - (pa->y < pb->y);
    return (pa->z > pb->z) - (pa->z < pb->z);
}

#define TEST_ASSERT(cond, pass_msg, fail_msg) do { \
    if (!(cond)) { \
        printf(RED "Test failed: " RESET "%s\n", fail_msg); \
        assert(cond); \
    } else { \
        printf(GREEN "Test passed: " RESET "%s\n", pass_msg); \
    } \
} while (0)

#define TEST_WARN(cond, msg) do { \
    if (!(cond)) { \
        printf(YELLOW "Warning: " RESET "%s\n", msg); \
    } \
} while (0)

void printPoint3D(void *p) {
    Point3D *pt = (Point3D *)p;
    printf("Point3D(%.1f, %.1f, %.1f)", pt->x, pt->y, pt->z);
}

void printPerson(void *p) {
    Person *pr = (Person *)p;
    printf("Person(%d, %s)", pr->id, pr->name);
}

int test_count = 0;
int pass_count = 0;

#define RUN_TEST(test_func) do { \
    printf("\n" YELLOW "Running %s..." RESET "\n", #test_func); \
    test_func(); \
    test_count++; \
    pass_count++; \
} while (0)

void test_create() {
    printf("  Testing array creation...\n");
    double *array = spda_create(double);
    TEST_ASSERT(spda_len(array) == 0, "Initial length is zero", "Expected initial length to be zero");
    TEST_ASSERT(spda_cap(array) == SPDA_DEFAULT_CAPACITY, "Initial capacity matches default", "Expected initial capacity to match SPDA_DEFAULT_CAPACITY");
    TEST_ASSERT(spda_stride(array) == sizeof(double), "Stride matches sizeof(double)", "Stride did not match sizeof(double)");
    spda_destroy(array);

    // Test reserve
    int *array2 = spda_reserve(int, 50);
    TEST_ASSERT(spda_len(array2) == 0, "Reserved array initial length is zero", "Expected initial length to be zero for reserved array");
    TEST_ASSERT(spda_cap(array2) >= 50, "Reserved capacity is at least requested", "Expected capacity to be at least 50");
    spda_destroy(array2);
}

void test_append() {
    printf("  Testing append operations...\n");
    double *array = spda_create(double);
    for (int i = 0; i < 10; i++) {
        spda_append(array, i + 0.5);
    }
    TEST_ASSERT(spda_len(array) == 10, "Length after 10 appends is correct", "Expected length to be 10 after append");
    TEST_ASSERT(spda_cap(array) >= 10, "Capacity is at least 10 after appends", "Capacity did not adjust correctly after append");

    // Test append_many
    spda_append_many(array, 10.5, 11.5, 12.5);
    TEST_ASSERT(spda_len(array) == 13, "Length after append_many is correct", "Expected length to be 13");

    // Test append_items
    double items[] = {13.5, 14.5};
    spda_append_items(array, items, 2);
    TEST_ASSERT(spda_len(array) == 15, "Length after append_items is correct", "Expected length to be 15");

    spda_destroy(array);
}

void test_insert() {
    printf("  Testing insert operation...\n");
    double *array = spda_create(double);
    for (int i = 0; i < 10; i++) {
        spda_append(array, i + 0.5);
    }
    spda_insert(array, 5, 100.75);
    TEST_ASSERT(spda_len(array) == 11, "Length after insert is correct", "Expected length to be 11 after insert");
    TEST_ASSERT(double_equals(array[5], 100.75), "Inserted value is at correct index", "Inserted value did not match expected value");

    // Test insert at beginning
    spda_insert(array, 0, -1.5);
    TEST_ASSERT(double_equals(array[0], -1.5), "Insert at beginning works", "Insert at index 0 failed");

    // Test insert at end
    spda_insert(array, spda_len(array), 999.9);
    TEST_ASSERT(double_equals(array[spda_len(array)-1], 999.9), "Insert at end works", "Insert at end failed");

    spda_destroy(array);
}

void test_remove() {
    printf("  Testing remove operations...\n");
    double *array = spda_create(double);
    for (int i = 0; i < 10; i++) {
        spda_append(array, i + 0.5);
    }
    double removed;
    spda_remove_ret(array, 5, &removed);
    TEST_ASSERT(spda_len(array) == 9, "Length after remove is correct", "Expected length to be 9 after remove");
    TEST_ASSERT(double_equals(removed, 5.5), "Removed value is correct", "Removed value did not match expected value");

    // Test remove at beginning
    spda_remove_ret(array, 0, &removed);
    TEST_ASSERT(double_equals(removed, 0.5), "Remove at beginning works", "Remove at index 0 failed");

    // Test remove at end
    spda_remove_ret(array, spda_len(array)-1, &removed);
    TEST_ASSERT(double_equals(removed, 9.5), "Remove at end works", "Remove at end failed");

    spda_destroy(array);
}

void test_reverse() {
    printf("  Testing reverse operation...\n");
    double *array = spda_create(double);
    for (int i = 0; i < 5; i++) {
        spda_append(array, i + 0.5);
    }
    spda_reverse(array);
    bool success = true;
    for (int i = 0; i < 5; i++) {
        if (!double_equals(array[i], 4.5 - i)) {
            success = false;
            break;
        }
    }
    TEST_ASSERT(success, "Reversed array contents are correct", "Array contents after reverse did not match expected");
    spda_destroy(array);
}

void test_pop() {
    printf("  Testing pop operation...\n");
    double *array = spda_create(double);
    for (int i = 0; i < 5; i++) {
        spda_append(array, i + 0.5);
    }
    double popped;
    spda_pop_ret(array, &popped);
    TEST_ASSERT(spda_len(array) == 4, "Length after pop is correct", "Expected length to be 4 after pop");
    TEST_ASSERT(double_equals(popped, 4.5), "Popped value is correct", "Popped value did not match expected value");

    // Test pop on empty (should not crash, but length remains 0)
    while (spda_len(array) > 0) spda_pop(array);
    TEST_ASSERT(spda_len(array) == 0, "Pop on empty array handled", "Pop on empty array failed");

    spda_destroy(array);
}

void test_clear() {
    printf("  Testing clear operation...\n");
    double *array = spda_create(double);
    for (int i = 0; i < 5; i++) {
        spda_append(array, i + 0.5);
    }
    spda_clear(array);
    TEST_ASSERT(spda_len(array) == 0, "Length after clear is zero", "Expected length to be zero after clear");
    spda_destroy(array);
}

void test_resize() {
    printf("  Testing resize operation...\n");
    double *array = spda_create(double);
    size_t old_cap = spda_cap(array);
    array = _spda_resize_def(array);  // Internal, but testing
    TEST_ASSERT(spda_cap(array) == old_cap * SPDA_GROWTH_FACTOR, "Capacity doubled after default resize", "Capacity did not grow by SPDA_GROWTH_FACTOR after resize");
    spda_destroy(array);
}

void test_sort() {
    printf("  Testing sort operations...\n");
    int *iarray = spda_create(int);
    spda_append_many(iarray, 3, 1, 4, 1, 5, 9, 2, 6, 5, 3);
    spda_sort(iarray, comparInt);
    bool success = true;
    for (size_t i = 1; i < spda_len(iarray); i++) {
        if (iarray[i - 1] > iarray[i]) {
            success = false;
            break;
        }
    }
    TEST_ASSERT(success, "Int array is sorted in ascending order", "Int array contents were not sorted correctly");
    spda_destroy(iarray);

    float *farray = spda_create(float);
    spda_append_many(farray, 3.1f, 1.2f, 4.3f, 1.4f, 5.5f);
    spda_sort(farray, comparFloat);
    success = true;
    for (size_t i = 1; i < spda_len(farray); i++) {
        if (farray[i - 1] > farray[i]) {
            success = false;
            break;
        }
    }
    TEST_ASSERT(success, "Float array is sorted", "Float array contents were not sorted correctly");
    spda_destroy(farray);

    double *darray = spda_create(double);
    spda_append_many(darray, 3.14, 1.41, 2.71, 0.58);
    spda_sort(darray, comparDouble);
    success = true;
    for (size_t i = 1; i < spda_len(darray); i++) {
        if (darray[i - 1] > darray[i]) {
            success = false;
            break;
        }
    }
    TEST_ASSERT(success, "Double array is sorted", "Double array contents were not sorted correctly");
    spda_destroy(darray);
}

void test_shrink() {
    printf("  Testing shrink operation...\n");
    int *array = spda_create(int);
    for (size_t i = 0; i < 100; i++) {
        spda_append(array, (int)i);
    }
    size_t original_cap = spda_cap(array);
    TEST_ASSERT(original_cap >= 100, "Capacity is sufficient for 100 elements", "Initial capacity is less than expected");

    for (size_t i = 0; i < 80; i++) {
        spda_pop(array);
    }
    TEST_ASSERT(spda_len(array) == 20, "Length is 20 after 80 pops", "Array length did not match expected value after pop");

    array = spda_shrink_to_fit(array);
    size_t shrunk_cap = spda_cap(array);
    TEST_ASSERT(shrunk_cap >= spda_len(array), "Shrunk capacity still fits all elements", "Shrunk capacity is smaller than current length");
    TEST_ASSERT(shrunk_cap < original_cap || shrunk_cap == SPDA_DEFAULT_CAPACITY, "Capacity is reduced after shrink_to_fit", "Capacity was not reduced after shrinking");
    TEST_ASSERT(spda_len(array) == 20, "Length unchanged after shrink_to_fit", "Array length changed unexpectedly after shrinking");
    spda_destroy(array);
}

void test_foreach() {
    printf("  Testing spda_foreach macro...\n");
    int *array = spda_create(int);
    spda_append_many(array, 1, 2, 3, 4, 5);

    int sum = 0;
    spda_foreach(int, item, array) {
        sum += *item;
    }
    TEST_ASSERT(sum == 15, "spda_foreach sums elements correctly", "spda_foreach failed to iterate correctly or compute the sum");

    // Test in-place mutation
    spda_foreach(int, item, array) {
        *item *= 2;
    }
    TEST_ASSERT(array[0] == 2 && array[4] == 10, "spda_foreach in-place mutation works correctly", "spda_foreach mutation did not modify the array");

    spda_destroy(array);
}

void test_copy() {
    printf("  Testing copy operation...\n");
    int *original = spda_create(int);
    spda_append_many(original, 1, 2, 3, 4, 5);
    int *copy = (int *)spda_copy(original);
    TEST_ASSERT(spda_len(copy) == spda_len(original), "Copy has same length", "Copy length does not match original");
    TEST_ASSERT(spda_cap(copy) == spda_cap(original), "Copy has same capacity", "Copy capacity does not match original");
    bool equal = true;
    for (size_t i = 0; i < spda_len(original); i++) {
        if (original[i] != copy[i]) {
            equal = false;
            break;
        }
    }
    TEST_ASSERT(equal, "Copy contents match original", "Copy contents do not match original");
    spda_destroy(original);
    spda_destroy(copy);
}

void test_structs() {
    printf("  Testing with structs...\n");
    Point3D *points = spda_create(Point3D);
    Point3D p1 = {1.0, 2.0, 3.0};
    Point3D p2 = {4.0, 5.0, 6.0};
    spda_append_many(points, p1, p2);
    TEST_ASSERT(spda_len(points) == 2, "Struct array length correct", "Struct array length incorrect");
    TEST_ASSERT(double_equals(points[0].x, 1.0) && double_equals(points[1].z, 6.0), "Struct values correct", "Struct values incorrect");

    // Test sort on structs
    spda_sort(points, comparPoint3D);
    TEST_ASSERT(double_equals(points[0].x, 1.0), "Struct sort works", "Struct sort failed");

    spda_destroy(points);
}

void test_random() {
    printf("  Testing random functions...\n");
    srand(time(NULL));
    int *rand_ints = spda_create(int);
    spda_rand(&rand_ints, 10, 0, 100);
    TEST_ASSERT(spda_len(rand_ints) == 10, "Random int array length correct", "Random int array length incorrect");
    for (size_t i = 0; i < spda_len(rand_ints); i++) {
        TEST_WARN(rand_ints[i] >= 0 && rand_ints[i] <= 100, "Random int in range");
    }
    spda_destroy(rand_ints);

    float *rand_floats = spda_create(float);
    spda_randf(&rand_floats, 10, 0.0f, 1.0f);
    TEST_ASSERT(spda_len(rand_floats) == 10, "Random float array length correct", "Random float array length incorrect");
    spda_destroy(rand_floats);
}

void test_stress() {
    printf("  Testing stress operations...\n");
    int *large_array = spda_create(int);
    for (int i = 0; i < 10000; i++) {
        spda_append(large_array, i);
    }
    TEST_ASSERT(spda_len(large_array) == 10000, "Large array append works", "Large array append failed");
    spda_sort(large_array, comparInt);
    TEST_ASSERT(large_array[0] == 0 && large_array[9999] == 9999, "Large array sort works", "Large array sort failed");
    spda_destroy(large_array);
}

int main(void) {
    printf("Starting comprehensive SPDA test suite...\n");

    RUN_TEST(test_create);
    RUN_TEST(test_append);
    RUN_TEST(test_insert);
    RUN_TEST(test_remove);
    RUN_TEST(test_reverse);
    RUN_TEST(test_pop);
    RUN_TEST(test_clear);
    RUN_TEST(test_resize);
    RUN_TEST(test_sort);
    RUN_TEST(test_shrink);
    RUN_TEST(test_foreach);
    RUN_TEST(test_copy);
    RUN_TEST(test_structs);
    RUN_TEST(test_random);
    RUN_TEST(test_stress);

    printf(GREEN "\nAll %d tests passed successfully!\n" RESET, test_count);
    return 0;
}