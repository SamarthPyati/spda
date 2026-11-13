/*  
 * Got this by Claude Opus 5.5
 * SPDA Test Suite
 * 
 *   ./test              run every test
 *   ./test insert copy  run tests whose name contains "insert" or "copy"
 *   ./test -l           list tests
 *   SPDA_TEST_NOFORK=1  run in-process (for lldb/gdb); a crash stops the run
 *
 * Adding a test: write `TEST(group, name) { ... }` anywhere in this file.
 * It registers itself; there is no separate list to update.
 *
 * Every test runs in its own forked process with a timeout, so a crash, an
 * ASan report or an infinite loop is reported as a failure of that one test
 * instead of taking down the whole suite. Output (including library error
 * messages) is captured and only shown for tests that fail.
 */

#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define SPDA_IMPLEMENTATION
#include "../spda.h"

/* ======================================================================== */
/* Framework                                                                */
/* ======================================================================== */

#define TEST_TIMEOUT_SEC 10
#define TEST_MAX         512
#define FAIL_EXIT_CODE   42   /* child exit code meaning "a CHECK failed" */

typedef struct {
    const char *name;
    void (*fn)(void);
} test_case;

static test_case g_tests[TEST_MAX];
static size_t    g_ntests;
static int       g_fails;     /* failed checks in the current test */

static void test_register(const char *name, void (*fn)(void)) {
    if (g_ntests < TEST_MAX) g_tests[g_ntests++] = (test_case){name, fn};
}

#define TEST(group, name)                                                    \
    static void test_##group##_##name(void);                                 \
    __attribute__((constructor)) static void reg_##group##_##name(void) {    \
        test_register(#group "/" #name, test_##group##_##name);              \
    }                                                                        \
    static void test_##group##_##name(void)

static void fail_at(const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

static void fail_at(const char *file, int line, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    printf("%s:%d: ", file, line);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
    g_fails++;
}

/* Non-fatal: record and continue. */
#define CHECK(cond) \
    do { if (!(cond)) fail_at(__FILE__, __LINE__, "CHECK(%s)", #cond); } while (0)

/* Fatal: record and leave the test (use when continuing would crash). */
#define REQUIRE(cond) \
    do { if (!(cond)) { fail_at(__FILE__, __LINE__, "REQUIRE(%s)", #cond); return; } } while (0)

#define CHECK_EQ(a, b)                                                        \
    do {                                                                      \
        long long a__ = (long long)(a), b__ = (long long)(b);                 \
        if (a__ != b__)                                                       \
            fail_at(__FILE__, __LINE__, "CHECK_EQ(%s, %s): %lld != %lld",     \
                    #a, #b, a__, b__);                                        \
    } while (0)

#define CHECK_DBL(a, b)                                                       \
    do {                                                                      \
        double a__ = (a), b__ = (b);                                          \
        if (!(a__ - b__ < 1e-9 && b__ - a__ < 1e-9))                          \
            fail_at(__FILE__, __LINE__, "CHECK_DBL(%s, %s): %g != %g",        \
                    #a, #b, a__, b__);                                        \
    } while (0)

/* Array invariants that must hold after every operation. */
#define CHECK_INVARIANTS(a)                                                   \
    do {                                                                      \
        CHECK(spda_len(a) <= spda_cap(a));                                    \
        CHECK_EQ(spda_stride(a), sizeof *(a));                                \
    } while (0)

/* Compare an int array against a literal list: CHECK_INTS(a, 1, 2, 3) */
#define CHECK_INTS(a, ...)                                                    \
    check_ints_at(__FILE__, __LINE__, #a, (a), (int[]){__VA_ARGS__},          \
                  sizeof((int[]){__VA_ARGS__}) / sizeof(int))

static void check_ints_at(const char *file, int line, const char *expr,
                          const int *a, const int *exp, size_t n) {
    if (spda_len(a) != n) {
        fail_at(file, line, "%s: length %zu, expected %zu", expr, spda_len(a), n);
        return;
    }
    for (size_t i = 0; i < n; i++) {
        if (a[i] != exp[i]) {
            fail_at(file, line, "%s[%zu] = %d, expected %d", expr, i, a[i], exp[i]);
            return;
        }
    }
}

/* ---- runner ------------------------------------------------------------ */

enum { R_PASS, R_FAIL, R_CRASH, R_TIMEOUT };

static int classify(int status, char *why, size_t n) {
    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);
        if (code == 0) return R_PASS;
        if (code == FAIL_EXIT_CODE) return R_FAIL;
        snprintf(why, n, "exited with code %d (sanitizer report or exit())", code);
        return R_CRASH;
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        if (sig == SIGALRM) {
            snprintf(why, n, "timed out after %ds (infinite loop?)", TEST_TIMEOUT_SEC);
            return R_TIMEOUT;
        }
        snprintf(why, n, "killed by signal %d (%s)", sig, strsignal(sig));
        return R_CRASH;
    }
    snprintf(why, n, "unknown status");
    return R_CRASH;
}

static void dump_captured(FILE *cap) {
    char line[512];
    int shown = 0, total = 0;
    rewind(cap);
    while (fgets(line, sizeof line, cap)) {
        if (shown < 25) { printf("        | %s", line); shown++; }
        total++;
    }
    if (total > shown) printf("        | ... (%d more lines)\n", total - shown);
}

static int run_forked(const test_case *t, char *why, size_t n) {
    FILE *cap = tmpfile();
    fflush(stdout);
    fflush(stderr);

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(2); }
    if (pid == 0) {
        if (cap) { dup2(fileno(cap), STDOUT_FILENO); dup2(fileno(cap), STDERR_FILENO); }
        alarm(TEST_TIMEOUT_SEC);
        g_fails = 0;
        t->fn();
        fflush(stdout);
        fflush(stderr);
        _exit(g_fails ? FAIL_EXIT_CODE : 0);
    }

    int status = 0;
    waitpid(pid, &status, 0);
    int r = classify(status, why, n);
    if (r != R_PASS && cap) dump_captured(cap);
    if (cap) fclose(cap);
    return r;
}

static int run_inline(const test_case *t) {
    g_fails = 0;
    t->fn();
    return g_fails ? R_FAIL : R_PASS;
}

static bool matches(const char *name, int argc, char **argv) {
    if (argc <= 1) return true;
    for (int i = 1; i < argc; i++)
        if (strstr(name, argv[i])) return true;
    return false;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "-l") == 0) {
        for (size_t i = 0; i < g_ntests; i++) puts(g_tests[i].name);
        return 0;
    }

    bool tty = isatty(STDOUT_FILENO);
    const char *GRN = tty ? "\x1b[32m" : "", *RED = tty ? "\x1b[31m" : "",
               *DIM = tty ? "\x1b[2m"  : "", *RST = tty ? "\x1b[0m"  : "";
    bool nofork = getenv("SPDA_TEST_NOFORK") != NULL;

    size_t ran = 0, failed = 0;
    const char *failed_names[TEST_MAX];

    for (size_t i = 0; i < g_ntests; i++) {
        const test_case *t = &g_tests[i];
        if (!matches(t->name, argc, argv)) continue;
        ran++;

        char why[128] = "";
        printf("%s....%s %s\n", DIM, RST, t->name);
        fflush(stdout);
        int r = nofork ? run_inline(t) : run_forked(t, why, sizeof why);

        /* overwrite the "...." line with the result */
        if (tty) printf("\x1b[1A\x1b[2K");
        if (r == R_PASS) {
            printf("%sPASS%s %s\n", GRN, RST, t->name);
        } else {
            printf("%sFAIL%s %s%s%s\n", RED, RST, t->name, *why ? " — " : "", why);
            failed_names[failed++] = t->name;
        }
    }

    printf("\n%zu tests, %s%zu passed%s, %s%zu failed%s\n", ran,
           GRN, ran - failed, RST, failed ? RED : "", failed, RST);
    for (size_t i = 0; i < failed; i++) printf("  - %s\n", failed_names[i]);
    if (ran == 0) { printf("no tests matched\n"); return 1; }
    return failed ? 1 : 0;
}

/* ======================================================================== */
/* Fixtures                                                                 */
/* ======================================================================== */

typedef struct { double x, y, z; } Point3D;
typedef struct { int id; char name[32]; } Person;
typedef struct { unsigned char bytes[512]; } Big;

static int cmp_int(const void *a, const void *b) {
    int x = *(const int *)a, y = *(const int *)b;
    return (x > y) - (x < y);          /* no overflow, unlike x - y */
}
static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}
static int cmp_person_id(const void *a, const void *b) {
    return cmp_int(&((const Person *)a)->id, &((const Person *)b)->id);
}

/* Array holding 0, 1, ..., n-1 */
static int *make_seq(size_t n) {
    int *a = spda_create(int);
    for (size_t i = 0; i < n; i++) spda_append(a, (int)i);
    return a;
}

static bool is_sorted_int(const int *a) {
    for (size_t i = 1; i < spda_len(a); i++)
        if (a[i - 1] > a[i]) return false;
    return true;
}

/* ======================================================================== */
/* Creation                                                                 */
/* ======================================================================== */

TEST(create, default_capacity) {
    double *a = spda_create(double);
    REQUIRE(a);
    CHECK_EQ(spda_len(a), 0);
    CHECK_EQ(spda_cap(a), SPDA_DEFAULT_CAPACITY);
    CHECK_EQ(spda_stride(a), sizeof(double));
    spda_destroy(a);
}

TEST(create, reserve_honours_large_request) {
    int *a = spda_reserve(int, 1000);
    REQUIRE(a);
    CHECK_EQ(spda_len(a), 0);
    CHECK(spda_cap(a) >= 1000);
    spda_destroy(a);
}

TEST(create, reserve_small_is_still_usable) {
    int *a = spda_reserve(int, 1);
    REQUIRE(a);
    CHECK(spda_cap(a) >= 1);
    for (int i = 0; i < 100; i++) spda_append(a, i);
    CHECK_EQ(spda_len(a), 100);
    CHECK_INVARIANTS(a);
    spda_destroy(a);
}

TEST(create, zero_stride_is_rejected) {
    CHECK(_spda_create(8, 0) == NULL);
}

TEST(create, struct_stride) {
    Point3D *p = spda_create(Point3D);
    Big *b = spda_create(Big);
    REQUIRE(p && b);
    CHECK_EQ(spda_stride(p), sizeof(Point3D));
    CHECK_EQ(spda_stride(b), sizeof(Big));
    spda_destroy(p);
    spda_destroy(b);
}

/* ======================================================================== */
/* NULL handling                                                            */
/* ======================================================================== */

TEST(null, accessors_return_zero) {
    int *a = NULL;
    CHECK_EQ(spda_len(a), 0);
    CHECK_EQ(spda_cap(a), 0);
    CHECK_EQ(spda_stride(a), 0);
}

TEST(null, destroy_is_noop) {
    int *a = NULL;
    spda_destroy(a);
}

TEST(null, append_does_not_crash) {
    int *a = NULL;
    spda_append(a, 1);
    spda_destroy(a);
}

TEST(null, insert_does_not_crash) {
    int *a = NULL;
    spda_insert(a, 0, 5);
    spda_destroy(a);
}

TEST(null, pop_is_safe) {
    int *a = NULL, out = 7;
    spda_pop(a);
    CHECK(!spda_pop_into(a, &out));
    CHECK_EQ(out, 7);
}

TEST(null, remove_is_safe) {
    int *a = NULL, out = 7;
    spda_remove(a, 0);
    spda_remove_into(a, 0, &out);
    CHECK_EQ(out, 7);
}

TEST(null, reverse_is_noop) {
    int *a = NULL;
    spda_reverse(a);
}

TEST(null, copy_returns_null) {
    int *a = NULL;
    CHECK(spda_copy(a) == NULL);
}

/* ======================================================================== */
/* Append                                                                   */
/* ======================================================================== */

TEST(append, single_value) {
    int *a = spda_create(int);
    spda_append(a, 42);
    CHECK_INTS(a, 42);
    spda_destroy(a);
}

TEST(append, grows_past_default_capacity) {
    int *a = spda_create(int);
    size_t n = SPDA_DEFAULT_CAPACITY * 10 + 3;
    for (size_t i = 0; i < n; i++) {
        spda_append(a, (int)i);
        REQUIRE(spda_len(a) == i + 1);
        REQUIRE(spda_cap(a) >= spda_len(a));
    }
    for (size_t i = 0; i < n; i++) REQUIRE(a[i] == (int)i);
    spda_destroy(a);
}

TEST(append, doubles) {
    double *a = spda_create(double);
    for (int i = 0; i < 100; i++) spda_append(a, i + 0.5);
    REQUIRE(spda_len(a) == 100);
    CHECK_DBL(a[0], 0.5);
    CHECK_DBL(a[99], 99.5);
    spda_destroy(a);
}

TEST(append, structs) {
    Point3D *a = spda_create(Point3D);
    for (int i = 0; i < 50; i++) {
        Point3D p = {i, i * 2.0, i * 3.0};
        spda_append(a, p);
    }
    REQUIRE(spda_len(a) == 50);
    CHECK_DBL(a[49].x, 49);
    CHECK_DBL(a[49].z, 147);
    CHECK_INVARIANTS(a);
    spda_destroy(a);
}

TEST(append, strings) {
    const char **a = spda_create(const char *);
    spda_append(a, "alpha");
    spda_append(a, "beta");
    REQUIRE(spda_len(a) == 2);
    CHECK(strcmp(a[0], "alpha") == 0);
    CHECK(strcmp(a[1], "beta") == 0);
    spda_destroy(a);
}

TEST(append, large_elements) {
    Big *a = spda_create(Big);
    for (int i = 0; i < 100; i++) {
        Big b;
        memset(b.bytes, i, sizeof b.bytes);
        spda_append(a, b);
    }
    REQUIRE(spda_len(a) == 100);
    CHECK_EQ(a[0].bytes[0], 0);
    CHECK_EQ(a[99].bytes[511], 99);
    spda_destroy(a);
}

/* Shrinking a 1-element array gives capacity 1; with growth factor 1.5,
 * 1 * 1.5 truncates back to 1, so the array can never grow again. */
TEST(append, after_shrink_to_tiny_capacity) {
    int *a = make_seq(1);
    a = spda_shrink_to_fit(a);
    for (int i = 1; i < 10; i++) {
        spda_append(a, i);
        REQUIRE(spda_cap(a) >= spda_len(a));
    }
    CHECK_INTS(a, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    spda_destroy(a);
}

TEST(append, after_resize_to_zero) {
    int *a = spda_create(int);
    a = _spda_resize(a, 0);
    REQUIRE(a);
    spda_append(a, 1);
    spda_append(a, 2);
    REQUIRE(spda_cap(a) >= spda_len(a));
    CHECK_INTS(a, 1, 2);
    spda_destroy(a);
}

/* ---- append_many / append_items ---------------------------------------- */

TEST(append_many, values_in_order) {
    int *a = spda_create(int);
    spda_append_many(a, 3, 1, 4, 1, 5);
    CHECK_INTS(a, 3, 1, 4, 1, 5);
    spda_destroy(a);
}

TEST(append_many, onto_existing_elements) {
    int *a = make_seq(3);
    spda_append_many(a, 10, 11);
    CHECK_INTS(a, 0, 1, 2, 10, 11);
    spda_destroy(a);
}

TEST(append_many, larger_than_capacity) {
    int *a = spda_create(int);
    int items[1000];
    for (int i = 0; i < 1000; i++) items[i] = i * 7;
    spda_append_items(a, items, 1000);
    REQUIRE(spda_len(a) == 1000);
    CHECK_INVARIANTS(a);
    CHECK(memcmp(a, items, sizeof items) == 0);
    spda_destroy(a);
}

TEST(append_many, structs) {
    Point3D *a = spda_create(Point3D);
    Point3D p1 = {1, 2, 3}, p2 = {4, 5, 6};
    spda_append_many(a, p1, p2);
    REQUIRE(spda_len(a) == 2);
    CHECK_DBL(a[1].y, 5);
    spda_destroy(a);
}

TEST(append_many, zero_count_is_noop) {
    int *a = make_seq(2);
    int items[] = {9};
    spda_append_items(a, items, 0);
    CHECK_INTS(a, 0, 1);
    spda_destroy(a);
}

TEST(append_many, null_items_is_noop) {
    int *a = make_seq(2);
    spda_append_items(a, NULL, 5);
    CHECK_INTS(a, 0, 1);
    spda_destroy(a);
}

TEST(append_many, after_shrink_to_tiny_capacity) {
    int *a = make_seq(1);
    a = spda_shrink_to_fit(a);
    spda_append_many(a, 1, 2, 3);
    REQUIRE(spda_cap(a) >= spda_len(a));
    CHECK_INTS(a, 0, 1, 2, 3);
    spda_destroy(a);
}

/* ======================================================================== */
/* Insert                                                                   */
/* ======================================================================== */

TEST(insert, into_empty) {
    int *a = spda_create(int);
    spda_insert(a, 0, 7);
    CHECK_INTS(a, 7);
    spda_destroy(a);
}

TEST(insert, front) {
    int *a = make_seq(3);
    spda_insert(a, 0, 99);
    CHECK_INTS(a, 99, 0, 1, 2);
    spda_destroy(a);
}

TEST(insert, middle) {
    int *a = make_seq(4);
    spda_insert(a, 2, 99);
    CHECK_INTS(a, 0, 1, 99, 2, 3);
    spda_destroy(a);
}

TEST(insert, end) {
    int *a = make_seq(3);
    spda_insert(a, (int)spda_len(a), 99);
    CHECK_INTS(a, 0, 1, 2, 99);
    spda_destroy(a);
}

TEST(insert, out_of_bounds_is_ignored) {
    int *a = make_seq(3);
    spda_insert(a, 4, 99);
    spda_insert(a, -1, 99);
    spda_insert(a, INT_MAX, 99);
    CHECK_INTS(a, 0, 1, 2);
    spda_destroy(a);
}

TEST(insert, at_full_capacity_grows) {
    int *a = make_seq(SPDA_DEFAULT_CAPACITY);
    REQUIRE(spda_len(a) == spda_cap(a));
    spda_insert(a, 0, -1);
    REQUIRE(spda_len(a) == SPDA_DEFAULT_CAPACITY + 1);
    CHECK_INVARIANTS(a);
    CHECK_EQ(a[0], -1);
    for (size_t i = 1; i < spda_len(a); i++) REQUIRE(a[i] == (int)i - 1);
    spda_destroy(a);
}

TEST(insert, many_at_front_reverses_order) {
    int *a = spda_create(int);
    for (int i = 0; i < 1000; i++) spda_insert(a, 0, i);
    REQUIRE(spda_len(a) == 1000);
    CHECK_INVARIANTS(a);
    for (int i = 0; i < 1000; i++) REQUIRE(a[i] == 999 - i);
    spda_destroy(a);
}

/* ======================================================================== */
/* Remove                                                                   */
/* ======================================================================== */

TEST(remove, front) {
    int *a = make_seq(4);
    spda_remove(a, 0);
    CHECK_INTS(a, 1, 2, 3);
    spda_destroy(a);
}

TEST(remove, middle) {
    int *a = make_seq(5);
    spda_remove(a, 2);
    CHECK_INTS(a, 0, 1, 3, 4);
    spda_destroy(a);
}

TEST(remove, back) {
    int *a = make_seq(3);
    spda_remove(a, 2);
    CHECK_INTS(a, 0, 1);
    spda_destroy(a);
}

TEST(remove, out_of_bounds_is_ignored) {
    int *a = make_seq(3);
    spda_remove(a, 3);
    spda_remove(a, -1);
    spda_remove(a, INT_MAX);
    CHECK_INTS(a, 0, 1, 2);
    spda_destroy(a);
}

TEST(remove, on_empty_is_ignored) {
    int *a = spda_create(int);
    spda_remove(a, 0);
    CHECK_EQ(spda_len(a), 0);
    spda_destroy(a);
}

TEST(remove, until_empty) {
    int *a = make_seq(50);
    while (spda_len(a) > 0) spda_remove(a, 0);
    CHECK_EQ(spda_len(a), 0);
    spda_append(a, 5);
    CHECK_INTS(a, 5);
    spda_destroy(a);
}

TEST(remove_ret, returns_removed_value) {
    int *a = make_seq(5);
    int out = -1;
    spda_remove_into(a, 3, &out);
    CHECK_EQ(out, 3);
    CHECK_INTS(a, 0, 1, 2, 4);
    spda_destroy(a);
}

TEST(remove_ret, out_of_bounds_leaves_dest_untouched) {
    int *a = make_seq(2);
    int out = 1234;
    spda_remove_into(a, 5, &out);
    CHECK_EQ(out, 1234);
    CHECK_INTS(a, 0, 1);
    spda_destroy(a);
}

TEST(remove_ret, null_dest_discards_value) {
    int *a = make_seq(3);
    spda_remove_into(a, 1, NULL);
    CHECK_INTS(a, 0, 2);
    spda_destroy(a);
}

/* ======================================================================== */
/* Remove range                                                             */
/* ======================================================================== */

TEST(remove_range, front) {
    int *a = make_seq(6);
    CHECK(spda_remove_range(a, 0, 2));
    CHECK_INTS(a, 2, 3, 4, 5);
    CHECK_INVARIANTS(a);
    spda_destroy(a);
}

TEST(remove_range, middle) {
    int *a = make_seq(8);
    CHECK(spda_remove_range(a, 2, 3));
    CHECK_INTS(a, 0, 1, 5, 6, 7);
    CHECK_INVARIANTS(a);
    spda_destroy(a);
}

TEST(remove_range, back) {
    int *a = make_seq(6);
    CHECK(spda_remove_range(a, 3, 3));
    CHECK_INTS(a, 0, 1, 2);
    spda_destroy(a);
}

TEST(remove_range, entire_array) {
    int *a = make_seq(5);
    CHECK(spda_remove_range(a, 0, 5));
    CHECK_EQ(spda_len(a), 0);
    spda_append(a, 9);
    CHECK_INTS(a, 9);
    spda_destroy(a);
}

TEST(remove_range, single_element_matches_remove) {
    int *a = make_seq(5);
    int *b = make_seq(5);
    spda_remove_range(a, 2, 1);
    spda_remove(b, 2);
    REQUIRE(spda_len(a) == spda_len(b));
    for (size_t i = 0; i < spda_len(a); i++) CHECK_EQ(a[i], b[i]);
    spda_destroy(a);
    spda_destroy(b);
}

TEST(remove_range, zero_count_is_noop) {
    int *a = make_seq(4);
    spda_remove_range(a, 1, 0);
    CHECK_INTS(a, 0, 1, 2, 3);
    spda_destroy(a);
}

/* Range starts in bounds but runs past the end: must be rejected whole,
   not partially applied, and must not underflow the length. */
TEST(remove_range, overrunning_end_is_rejected) {
    int *a = make_seq(5);
    CHECK(!spda_remove_range(a, 3, 5));
    CHECK_INTS(a, 0, 1, 2, 3, 4);
    CHECK_INVARIANTS(a);
    spda_destroy(a);
}

TEST(remove_range, start_out_of_bounds_is_rejected) {
    int *a = make_seq(3);
    CHECK(!spda_remove_range(a, 3, 1));
    CHECK(!spda_remove_range(a, 10, 2));
    CHECK_INTS(a, 0, 1, 2);
    spda_destroy(a);
}

/* idx + count wraps around SIZE_MAX; a naive `idx + count > len` check
   would pass and corrupt memory. */
TEST(remove_range, count_overflow_is_rejected) {
    int *a = make_seq(4);
    CHECK(!spda_remove_range(a, 1, SIZE_MAX));
    CHECK(!spda_remove_range(a, 2, SIZE_MAX - 1));
    CHECK_INTS(a, 0, 1, 2, 3);
    spda_destroy(a);
}

TEST(remove_range, on_empty_is_rejected) {
    int *a = spda_create(int);
    CHECK(!spda_remove_range(a, 0, 1));
    CHECK_EQ(spda_len(a), 0);
    spda_destroy(a);
}

TEST(remove_range, null_array_is_safe) {
    int *a = NULL;
    CHECK(!spda_remove_range(a, 0, 1));
}

TEST(remove_range, repeated_until_empty) {
    int *a = make_seq(30);
    while (spda_len(a) >= 4) REQUIRE(spda_remove_range(a, 1, 3));
    CHECK_INTS(a, 0, 28, 29);
    spda_destroy(a);
}

TEST(remove_range, structs) {
    Person *p = spda_create(Person);
    for (int i = 0; i < 6; i++) spda_append(p, ((Person){.id = i}));
    REQUIRE(spda_remove_range(p, 1, 4));
    REQUIRE(spda_len(p) == 2);
    CHECK_EQ(p[0].id, 0);
    CHECK_EQ(p[1].id, 5);
    CHECK_INVARIANTS(p);
    spda_destroy(p);
}

/* ======================================================================== */
/* Swap remove                                                              */
/* ======================================================================== */

TEST(swap_remove, front_takes_last) {
    int *a = make_seq(5);
    CHECK(spda_swap_remove(a, 0));
    CHECK_INTS(a, 4, 1, 2, 3);
    CHECK_INVARIANTS(a);
    spda_destroy(a);
}

TEST(swap_remove, middle_takes_last) {
    int *a = make_seq(5);
    CHECK(spda_swap_remove(a, 2));
    CHECK_INTS(a, 0, 1, 4, 3);
    spda_destroy(a);
}

/* Removing the last element must behave like pop (slot == last path). */
TEST(swap_remove, last_element_is_plain_pop) {
    int *a = make_seq(4);
    CHECK(spda_swap_remove(a, 3));
    CHECK_INTS(a, 0, 1, 2);
    spda_destroy(a);
}

TEST(swap_remove, single_element_array) {
    int *a = make_seq(1);
    CHECK(spda_swap_remove(a, 0));
    CHECK_EQ(spda_len(a), 0);
    spda_append(a, 7);
    CHECK_INTS(a, 7);
    spda_destroy(a);
}

TEST(swap_remove, into_returns_removed_value) {
    int *a = make_seq(5);
    int out = -1;
    CHECK(spda_swap_remove_into(a, 1, &out));
    CHECK_EQ(out, 1);
    CHECK_INTS(a, 0, 4, 2, 3);
    spda_destroy(a);
}

/* dest must be captured before the gap is overwritten by the last element. */
TEST(swap_remove, into_last_returns_last_value) {
    int *a = make_seq(3);
    int out = -1;
    CHECK(spda_swap_remove_into(a, 2, &out));
    CHECK_EQ(out, 2);
    CHECK_INTS(a, 0, 1);
    spda_destroy(a);
}

TEST(swap_remove, out_of_bounds_is_rejected) {
    int *a = make_seq(3);
    int out = 1234;
    CHECK(!spda_swap_remove(a, 3));
    CHECK(!spda_swap_remove(a, SIZE_MAX));
    CHECK(!spda_swap_remove_into(a, 5, &out));
    CHECK_EQ(out, 1234);
    CHECK_INTS(a, 0, 1, 2);
    spda_destroy(a);
}

TEST(swap_remove, on_empty_is_rejected) {
    int *a = spda_create(int);
    CHECK(!spda_swap_remove(a, 0));
    CHECK_EQ(spda_len(a), 0);
    spda_destroy(a);
}

TEST(swap_remove, null_array_is_safe) {
    int *a = NULL;
    int out = 1234;
    CHECK(!spda_swap_remove(a, 0));
    CHECK(!spda_swap_remove_into(a, 0, &out));
    CHECK_EQ(out, 1234);
}

/* Order may change, but the multiset of survivors must be exact. */
TEST(swap_remove, preserves_remaining_elements) {
    enum { N = 40 };
    int *a = make_seq(N);
    bool removed[N] = {0};
    for (size_t k = 0; k < N / 2; k++) {
        size_t idx = (k * 7) % spda_len(a);
        removed[a[idx]] = true;
        REQUIRE(spda_swap_remove(a, idx));
    }
    REQUIRE(spda_len(a) == N / 2);
    int seen[N] = {0};
    for (size_t i = 0; i < spda_len(a); i++) {
        REQUIRE(a[i] >= 0 && a[i] < N);
        seen[a[i]]++;
    }
    for (int v = 0; v < N; v++) CHECK_EQ(seen[v], removed[v] ? 0 : 1);
    spda_destroy(a);
}

TEST(swap_remove, until_empty) {
    int *a = make_seq(25);
    while (spda_len(a) > 0) REQUIRE(spda_swap_remove(a, 0));
    CHECK_EQ(spda_len(a), 0);
    CHECK_INVARIANTS(a);
    spda_destroy(a);
}

TEST(swap_remove, structs) {
    Person *p = spda_create(Person);
    for (int i = 0; i < 4; i++) spda_append(p, ((Person){.id = i}));
    Person out = {.id = -1};
    REQUIRE(spda_swap_remove_into(p, 1, &out));
    CHECK_EQ(out.id, 1);
    REQUIRE(spda_len(p) == 3);
    CHECK_EQ(p[0].id, 0);
    CHECK_EQ(p[1].id, 3);
    CHECK_EQ(p[2].id, 2);
    CHECK_INVARIANTS(p);
    spda_destroy(p);
}

/* ======================================================================== */
/* Pop                                                                      */
/* ======================================================================== */

TEST(pop, plain_pop_decrements_length) {
    int *a = make_seq(3);
    spda_pop(a);
    CHECK_INTS(a, 0, 1);
    spda_destroy(a);
}

TEST(pop, returns_in_lifo_order) {
    int *a = make_seq(5);
    int out;
    for (int expect = 4; expect >= 0; expect--) {
        REQUIRE(spda_pop_into(a, &out));
        CHECK_EQ(out, expect);
    }
    CHECK_EQ(spda_len(a), 0);
    spda_destroy(a);
}

TEST(pop, null_dest_discards_value) {
    int *a = make_seq(2);
    CHECK(spda_pop_into(a, NULL));
    CHECK_INTS(a, 0);
    spda_destroy(a);
}

TEST(pop, on_empty_returns_false) {
    int *a = spda_create(int);
    int out = 77;
    CHECK(!spda_pop_into(a, &out));
    CHECK_EQ(out, 77);
    spda_pop(a);
    CHECK_EQ(spda_len(a), 0);
    spda_destroy(a);
}

/* ======================================================================== */
/* Reverse                                                                  */
/* ======================================================================== */

TEST(reverse, empty) {
    int *a = spda_create(int);
    spda_reverse(a);
    CHECK_EQ(spda_len(a), 0);
    spda_destroy(a);
}

TEST(reverse, single) {
    int *a = make_seq(1);
    spda_reverse(a);
    CHECK_INTS(a, 0);
    spda_destroy(a);
}

TEST(reverse, even_length) {
    int *a = make_seq(4);
    spda_reverse(a);
    CHECK_INTS(a, 3, 2, 1, 0);
    spda_destroy(a);
}

TEST(reverse, odd_length) {
    int *a = make_seq(5);
    spda_reverse(a);
    CHECK_INTS(a, 4, 3, 2, 1, 0);
    spda_destroy(a);
}

TEST(reverse, twice_is_identity) {
    int *a = make_seq(37);
    spda_reverse(a);
    spda_reverse(a);
    for (int i = 0; i < 37; i++) REQUIRE(a[i] == i);
    spda_destroy(a);
}

TEST(reverse, structs) {
    Point3D *a = spda_create(Point3D);
    Point3D p1 = {1, 1, 1}, p2 = {2, 2, 2}, p3 = {3, 3, 3};
    spda_append_many(a, p1, p2, p3);
    spda_reverse(a);
    CHECK_DBL(a[0].x, 3);
    CHECK_DBL(a[2].z, 1);
    spda_destroy(a);
}

/* ======================================================================== */
/* Clear                                                                    */
/* ======================================================================== */

TEST(clear, resets_length_keeps_capacity) {
    int *a = make_seq(100);
    size_t cap = spda_cap(a);
    spda_clear(a);
    CHECK_EQ(spda_len(a), 0);
    CHECK_EQ(spda_cap(a), cap);
    spda_destroy(a);
}

TEST(clear, array_reusable_after_clear) {
    int *a = make_seq(10);
    spda_clear(a);
    spda_append_many(a, 5, 6);
    CHECK_INTS(a, 5, 6);
    spda_destroy(a);
}

/* ======================================================================== */
/* Resize / shrink                                                          */
/* ======================================================================== */

TEST(resize, grow_preserves_data) {
    int *a = make_seq(10);
    a = _spda_resize(a, 500);
    REQUIRE(a);
    CHECK_EQ(spda_cap(a), 500);
    for (int i = 0; i < 10; i++) REQUIRE(a[i] == i);
    spda_destroy(a);
}

TEST(resize, below_length_clamps_length) {
    int *a = make_seq(10);
    a = _spda_resize(a, 4);
    REQUIRE(a);
    CHECK_EQ(spda_cap(a), 4);
    CHECK_INTS(a, 0, 1, 2, 3);
    spda_destroy(a);
}

TEST(resize, default_growth_increases_capacity) {
    int *a = spda_create(int);
    size_t before = spda_cap(a);
    a = _spda_resize_def(a);
    REQUIRE(a);
    CHECK(spda_cap(a) > before);
    spda_destroy(a);
}

TEST(shrink, underused_array_shrinks) {
    int *a = make_seq(1000);
    while (spda_len(a) > 20) spda_pop(a);
    size_t before = spda_cap(a);
    a = spda_shrink_to_fit(a);
    REQUIRE(a);
    CHECK(spda_cap(a) < before);
    CHECK(spda_cap(a) >= spda_len(a));
    for (int i = 0; i < 20; i++) REQUIRE(a[i] == i);
    spda_destroy(a);
}

TEST(shrink, well_used_array_unchanged) {
    int *a = make_seq(SPDA_DEFAULT_CAPACITY);
    size_t before = spda_cap(a);
    int *same = spda_shrink_to_fit(a);
    CHECK(same == a);
    CHECK_EQ(spda_cap(same), before);
    spda_destroy(same);
}

TEST(shrink, empty_array_is_safe) {
    int *a = spda_create(int);
    a = spda_shrink_to_fit(a);
    REQUIRE(a);
    CHECK_EQ(spda_len(a), 0);
    spda_destroy(a);
}

TEST(shrink, then_grow_again) {
    int *a = make_seq(1000);
    while (spda_len(a) > 20) spda_pop(a);
    a = spda_shrink_to_fit(a);
    for (int i = 20; i < 2000; i++) spda_append(a, i);
    REQUIRE(spda_len(a) == 2000);
    CHECK_INVARIANTS(a);
    for (int i = 0; i < 2000; i++) REQUIRE(a[i] == i);
    spda_destroy(a);
}

/* ======================================================================== */
/* Copy                                                                     */
/* ======================================================================== */

TEST(copy, same_contents_and_metadata) {
    int *a = make_seq(10);
    int *c = spda_copy(a);
    REQUIRE(c && c != a);
    CHECK_EQ(spda_len(c), spda_len(a));
    CHECK_EQ(spda_stride(c), spda_stride(a));
    CHECK(memcmp(a, c, 10 * sizeof(int)) == 0);
    spda_destroy(a);
    spda_destroy(c);
}

TEST(copy, is_independent) {
    int *a = make_seq(3);
    int *c = spda_copy(a);
    REQUIRE(c);
    c[0] = 100;
    spda_append(c, 3);
    a[1] = 200;
    CHECK_INTS(a, 0, 200, 2);
    CHECK_INTS(c, 100, 1, 2, 3);
    spda_destroy(a);
    spda_destroy(c);
}

TEST(copy, empty_array) {
    int *a = spda_create(int);
    int *c = spda_copy(a);
    REQUIRE(c);
    CHECK_EQ(spda_len(c), 0);
    spda_destroy(a);
    spda_destroy(c);
}

TEST(copy, of_shrunk_array_can_grow) {
    int *a = make_seq(1000);
    while (spda_len(a) > 10) spda_pop(a);
    a = spda_shrink_to_fit(a);
    int *c = spda_copy(a);
    REQUIRE(c);
    for (int i = 10; i < 500; i++) spda_append(c, i);
    REQUIRE(spda_len(c) == 500);
    CHECK_INVARIANTS(c);
    for (int i = 0; i < 500; i++) REQUIRE(c[i] == i);
    CHECK_EQ(spda_len(a), 10);
    spda_destroy(a);
    spda_destroy(c);
}

/* ======================================================================== */
/* Sort                                                                     */
/* ======================================================================== */

TEST(sort, ints_with_duplicates_and_extremes) {
    int *a = spda_create(int);
    spda_append_many(a, 5, INT_MIN, 3, 3, INT_MAX, -1, 0, 5);
    spda_sort(a, cmp_int);
    CHECK_INTS(a, INT_MIN, -1, 0, 3, 3, 5, 5, INT_MAX);
    spda_destroy(a);
}

TEST(sort, empty_and_single) {
    int *a = spda_create(int);
    spda_sort(a, cmp_int);
    CHECK_EQ(spda_len(a), 0);
    spda_append(a, 1);
    spda_sort(a, cmp_int);
    CHECK_INTS(a, 1);
    spda_destroy(a);
}

TEST(sort, reverse_sorted_input) {
    int *a = make_seq(500);
    spda_reverse(a);
    spda_sort(a, cmp_int);
    CHECK(is_sorted_int(a));
    CHECK_EQ(a[0], 0);
    CHECK_EQ(a[499], 499);
    spda_destroy(a);
}

TEST(sort, doubles) {
    double *a = spda_create(double);
    spda_append_many(a, 3.14, -2.5, 1.41, 0.0);
    spda_sort(a, cmp_double);
    CHECK_DBL(a[0], -2.5);
    CHECK_DBL(a[3], 3.14);
    spda_destroy(a);
}

TEST(sort, strings) {
    const char **a = spda_create(const char *);
    spda_append_many(a, "pear", "apple", "fig");
    spda_sort(a, cmp_str);
    CHECK(strcmp(a[0], "apple") == 0);
    CHECK(strcmp(a[2], "pear") == 0);
    spda_destroy(a);
}

TEST(sort, structs_by_key) {
    Person *a = spda_create(Person);
    Person p1 = {3, "carol"}, p2 = {1, "alice"}, p3 = {2, "bob"};
    spda_append_many(a, p1, p2, p3);
    spda_sort(a, cmp_person_id);
    CHECK(strcmp(a[0].name, "alice") == 0);
    CHECK(strcmp(a[2].name, "carol") == 0);
    spda_destroy(a);
}

/* ======================================================================== */
/* Iteration                                                                */
/* ======================================================================== */

TEST(foreach, visits_all_in_order) {
    int *a = make_seq(10);
    int expect = 0;
    spda_foreach(int, it, a) CHECK_EQ(*it, expect++);
    CHECK_EQ(expect, 10);
    spda_destroy(a);
}

TEST(foreach, empty_array_has_no_iterations) {
    int *a = spda_create(int);
    int n = 0;
    spda_foreach(int, it, a) { (void)it; n++; }
    CHECK_EQ(n, 0);
    spda_destroy(a);
}

TEST(foreach, mutates_in_place) {
    int *a = make_seq(5);
    spda_foreach(int, it, a) *it *= 2;
    CHECK_INTS(a, 0, 2, 4, 6, 8);
    spda_destroy(a);
}

/* ======================================================================== */
/* Memory layout                                                            */
/* ======================================================================== */

TEST(layout, elements_aligned_for_any_type) {
    long double *a = spda_create(long double);
    REQUIRE(a);
    CHECK_EQ((uintptr_t)a % _Alignof(max_align_t), 0);
    spda_append(a, 1.5L);
    CHECK(a[0] == 1.5L);
    spda_destroy(a);
}

/* ======================================================================== */
/* Printing                                                                 */
/* ======================================================================== */

static char g_print_buf[8192];

/* Read everything written to `f` since it was opened, then close it. */
static const char *print_capture_end(FILE *f) {
    fflush(f);
    rewind(f);
    size_t n = fread(g_print_buf, 1, sizeof g_print_buf - 1, f);
    g_print_buf[n] = '\0';
    fclose(f);
    return g_print_buf;
}

/* CHECK_PRINTS(stmt using `f`, "expected") */
#define CHECK_PRINTS(stmt, expected)                                          \
    do {                                                                      \
        FILE *f = tmpfile();                                                  \
        REQUIRE(f);                                                           \
        stmt;                                                                 \
        const char *got__ = print_capture_end(f);                             \
        if (strcmp(got__, (expected)) != 0)                                   \
            fail_at(__FILE__, __LINE__, "printed \"%s\", expected \"%s\"",    \
                    got__, (expected));                                       \
    } while (0)

static void fmt_person(FILE *o, const void *e) {
    const Person *p = e;
    fprintf(o, "{%d, %s}", p->id, p->name);
}

TEST(print, ints) {
    int *a = spda_create(int);
    spda_append_many(a, 0, -1, 42, INT_MAX, INT_MIN);
    CHECK_PRINTS(spda_fprint(f, a), "[0, -1, 42, 2147483647, -2147483648]");
    spda_destroy(a);
}

TEST(print, empty_array) {
    int *a = spda_create(int);
    CHECK_PRINTS(spda_fprint(f, a), "[]");
    spda_destroy(a);
}

TEST(print, single_element_has_no_separator) {
    int *a = spda_create(int);
    spda_append(a, 7);
    CHECK_PRINTS(spda_fprint(f, a), "[7]");
    spda_destroy(a);
}

TEST(print, null_array_prints_null) {
    int *a = NULL;
    CHECK_PRINTS(spda_fprint(f, a), "NULL");
}

/* fprint must not add a newline, so it can be embedded in a line. */
TEST(print, fprint_composes_inline) {
    int *a = make_seq(3);
    CHECK_PRINTS((fputs("x = ", f), spda_fprint(f, a), fputs(" end", f)),
                 "x = [0, 1, 2] end");
    spda_destroy(a);
}

TEST(print, long_and_size_t) {
    long *l = spda_create(long);
    spda_append_many(l, -5L, LONG_MAX);
    CHECK_PRINTS(spda_fprint(f, l), "[-5, 9223372036854775807]");
    size_t *s = spda_create(size_t);
    spda_append_many(s, (size_t)0, (size_t)123);
    CHECK_PRINTS(spda_fprint(f, s), "[0, 123]");
    spda_destroy(l);
    spda_destroy(s);
}

TEST(print, floats_and_doubles) {
    float *a = spda_create(float);
    spda_append_many(a, 1.5f, -0.25f, 0.0f);
    CHECK_PRINTS(spda_fprint(f, a), "[1.5, -0.25, 0]");
    double *d = spda_create(double);
    spda_append_many(d, 3.0, 1e20, -2.5e-7);
    CHECK_PRINTS(spda_fprint(f, d), "[3, 1e+20, -2.5e-07]");
    spda_destroy(a);
    spda_destroy(d);
}

TEST(print, chars_are_quoted) {
    char *a = spda_create(char);
    spda_append_many(a, 'a', 'Z', ' ');
    CHECK_PRINTS(spda_fprint(f, a), "['a', 'Z', ' ']");
    spda_destroy(a);
}

TEST(print, strings_quoted_and_null_safe) {
    const char **s = spda_create(const char *);
    spda_append_many(s, "hi", "", (const char *)NULL);
    CHECK_PRINTS(spda_fprint(f, s), "[\"hi\", \"\", NULL]");
    char **m = spda_create(char *);
    spda_append(m, (char *)"mut");
    CHECK_PRINTS(spda_fprint(f, m), "[\"mut\"]");
    spda_destroy(s);
    spda_destroy(m);
}

TEST(print, custom_printer_for_structs) {
    Person *p = spda_create(Person);
    spda_append(p, ((Person){.id = 1, .name = "ann"}));
    spda_append(p, ((Person){.id = 2, .name = "bo"}));
    CHECK_PRINTS(spda_fprint_with(f, p, fmt_person), "[{1, ann}, {2, bo}]");
    spda_destroy(p);
}

static void fmt_hex(FILE *o, const void *e) { fprintf(o, "0x%x", *(const unsigned *)e); }

TEST(print, custom_printer_overrides_default) {
    int *a = spda_create(int);
    spda_append_many(a, 10, 255);
    CHECK_PRINTS(spda_fprint_with(f, a, fmt_hex), "[0xa, 0xff]");
    spda_destroy(a);
}

TEST(print, null_printer_is_safe) {
    int *a = make_seq(3);
    CHECK_PRINTS(spda_fprint_with(f, a, NULL), "");
    spda_destroy(a);
}

TEST(print, null_stream_is_safe) {
    int *a = make_seq(3);
    spda_fprint(NULL, a);
    spda_fprint_with(NULL, a, spda_fmt_int);
    CHECK_INTS(a, 0, 1, 2);
    spda_destroy(a);
}

TEST(print, does_not_modify_array) {
    int *a = make_seq(4);
    size_t cap = spda_cap(a);
    CHECK_PRINTS(spda_fprint(f, a), "[0, 1, 2, 3]");
    CHECK_INTS(a, 0, 1, 2, 3);
    CHECK_EQ(spda_cap(a), cap);
    spda_destroy(a);
}

TEST(print, many_elements_separator_count) {
    int *a = make_seq(500);
    FILE *f = tmpfile();
    REQUIRE(f);
    spda_fprint(f, a);
    const char *out = print_capture_end(f);
    size_t commas = 0;
    for (const char *c = out; *c; c++) commas += (*c == ',');
    CHECK_EQ(commas, 499);
    CHECK(out[0] == '[');
    CHECK(out[strlen(out) - 1] == ']');
    spda_destroy(a);
}

TEST(print, unsigned_types) {
    unsigned *u = spda_create(unsigned);
    spda_append_many(u, 0u, UINT_MAX);
    CHECK_PRINTS(spda_fprint(f, u), "[0, 4294967295]");
    unsigned long long *ull = spda_create(unsigned long long);
    spda_append(ull, ULLONG_MAX);
    CHECK_PRINTS(spda_fprint(f, ull), "[18446744073709551615]");
    unsigned short *us = spda_create(unsigned short);
    spda_append(us, (unsigned short)65535);
    CHECK_PRINTS(spda_fprint(f, us), "[65535]");
    spda_destroy(u);
    spda_destroy(ull);
    spda_destroy(us);
}

TEST(print, signed_small_and_wide_types) {
    short *s = spda_create(short);
    spda_append_many(s, (short)-3, (short)SHRT_MAX);
    CHECK_PRINTS(spda_fprint(f, s), "[-3, 32767]");
    long long *ll = spda_create(long long);
    spda_append(ll, LLONG_MIN);
    CHECK_PRINTS(spda_fprint(f, ll), "[-9223372036854775808]");
    spda_destroy(s);
    spda_destroy(ll);
}

/* Byte types print as numbers, unlike plain char. */
TEST(print, byte_types_print_as_numbers) {
    unsigned char *u = spda_create(unsigned char);
    spda_append_many(u, (unsigned char)0, (unsigned char)255);
    CHECK_PRINTS(spda_fprint(f, u), "[0, 255]");
    signed char *s = spda_create(signed char);
    spda_append(s, (signed char)-128);
    CHECK_PRINTS(spda_fprint(f, s), "[-128]");
    spda_destroy(u);
    spda_destroy(s);
}

TEST(print, bools) {
    bool *b = spda_create(bool);
    spda_append_many(b, true, false);
    CHECK_PRINTS(spda_fprint(f, b), "[true, false]");
    spda_destroy(b);
}

TEST(print, long_double) {
    long double *a = spda_create(long double);
    spda_append_many(a, 1.5L, -2.0L);
    CHECK_PRINTS(spda_fprint(f, a), "[1.5, -2]");
    spda_destroy(a);
}

/* ======================================================================== */
/* Randomised comparison against a plain C array                            */
/* ======================================================================== */

TEST(fuzz, matches_reference_model) {
    enum { MAXN = 4096, STEPS = 50000 };
    static int model[MAXN];
    size_t n = 0;
    int *a = spda_create(int);
    srand(12345);

    for (int step = 0; step < STEPS; step++) {
        int v = rand();
        switch (rand() % 8) {
        case 0: case 1: case 2:                                   /* append */
            if (n < MAXN) { spda_append(a, v); model[n++] = v; }
            break;
        case 3:                                                   /* insert */
            if (n < MAXN) {
                size_t i = (size_t)rand() % (n + 1);
                spda_insert(a, (int)i, v);
                memmove(model + i + 1, model + i, (n - i) * sizeof(int));
                model[i] = v;
                n++;
            }
            break;
        case 4:                                               /* remove_ret */
            if (n) {
                size_t i = (size_t)rand() % n;
                int out = 0;
                spda_remove_into(a, (int)i, &out);
                REQUIRE(out == model[i]);
                memmove(model + i, model + i + 1, (n - i - 1) * sizeof(int));
                n--;
            }
            break;
        case 5:                                                  /* pop_ret */
            if (n) {
                int out = 0;
                REQUIRE(spda_pop_into(a, &out));
                REQUIRE(out == model[--n]);
            }
            break;
        case 6:                                                  /* reverse */
            if (rand() % 64 == 0) {
                spda_reverse(a);
                for (size_t i = 0; i < n / 2; i++) {
                    int t = model[i]; model[i] = model[n - 1 - i]; model[n - 1 - i] = t;
                }
            }
            break;
        case 7:                                             /* append_many */
            if (n + 3 <= MAXN) {
                spda_append_many(a, v, v + 1, v + 2);
                model[n++] = v; model[n++] = v + 1; model[n++] = v + 2;
            }
            break;
        }
        REQUIRE(spda_len(a) == n);
        REQUIRE(spda_cap(a) >= n);
    }
    REQUIRE(n == 0 || memcmp(a, model, n * sizeof(int)) == 0);
    spda_destroy(a);
}
