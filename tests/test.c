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

/* The runner uses POSIX (fork, strsignal, fileno, dup2). glibc hides these
   under strict -std=c11/c17 unless a POSIX level is requested. */
#define _POSIX_C_SOURCE 200809L

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

/* Fault injection: fail_alloc_after(n) makes the allocation n calls from now
   fail (0 = the very next one), once. Lets tests reach out-of-memory paths. */
static long g_alloc_fail_in = -1;

static int alloc_should_fail(void) {
    if (g_alloc_fail_in < 0) return 0;
    return g_alloc_fail_in-- == 0;
}

static void fail_alloc_after(long n) { g_alloc_fail_in = n; }

#define SPDA_MALLOC(size)       (alloc_should_fail() ? NULL : malloc(size))
#define SPDA_REALLOC(ptr, size) (alloc_should_fail() ? NULL : realloc((ptr), (size)))
#define SPDA_FREE(ptr)          free(ptr)

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

/* Elements are swapped through a 64-byte stack buffer in chunks. These cover
   elements larger than one chunk, sizes that leave a partial last chunk, and
   exactly one chunk: every byte of each element must move together. */

/* Fill element `e` of `size` bytes with a pattern unique to (tag, byte). */
static void fill_pattern(unsigned char *e, size_t size, int tag) {
    for (size_t k = 0; k < size; k++) e[k] = (unsigned char)(tag * 31 + k * 7);
}

static bool has_pattern(const unsigned char *e, size_t size, int tag) {
    for (size_t k = 0; k < size; k++)
        if (e[k] != (unsigned char)(tag * 31 + k * 7)) return false;
    return true;
}

TEST(reverse, large_elements_multi_chunk) {
    Big *a = spda_create(Big);                     /* 512 bytes = 8 chunks */
    for (int t = 0; t < 5; t++) {
        Big b;
        fill_pattern(b.bytes, sizeof b.bytes, t);
        spda_append(a, b);
    }
    spda_reverse(a);
    for (int i = 0; i < 5; i++)
        CHECK(has_pattern(a[i].bytes, sizeof a[i].bytes, 4 - i));
    spda_destroy(a);
}

TEST(reverse, partial_last_chunk) {
    typedef struct { unsigned char bytes[100]; } Odd;   /* 64 + 36 */
    Odd *a = spda_create(Odd);
    for (int t = 0; t < 4; t++) {
        Odd o;
        fill_pattern(o.bytes, sizeof o.bytes, t);
        spda_append(a, o);
    }
    spda_reverse(a);
    for (int i = 0; i < 4; i++)
        CHECK(has_pattern(a[i].bytes, sizeof a[i].bytes, 3 - i));
    spda_destroy(a);
}

TEST(reverse, exactly_one_chunk) {
    typedef struct { unsigned char bytes[64]; } Chunk;
    Chunk *a = spda_create(Chunk);
    for (int t = 0; t < 3; t++) {
        Chunk c;
        fill_pattern(c.bytes, sizeof c.bytes, t);
        spda_append(a, c);
    }
    spda_reverse(a);
    for (int i = 0; i < 3; i++)
        CHECK(has_pattern(a[i].bytes, sizeof a[i].bytes, 2 - i));
    spda_destroy(a);
}

TEST(reverse, single_byte_elements) {
    char *a = spda_create(char);
    spda_append_many(a, 'a', 'b', 'c', 'd', 'e');
    spda_reverse(a);
    CHECK(a[0] == 'e' && a[1] == 'd' && a[2] == 'c' && a[3] == 'b' && a[4] == 'a');
    spda_destroy(a);
}

/* Large array: every element lands in its mirrored position. */
TEST(reverse, many_elements) {
    int *a = make_seq(10001);
    spda_reverse(a);
    for (int i = 0; i < 10001; i++) REQUIRE(a[i] == 10000 - i);
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

/* ISSUES.md B4. Elements start sizeof(spda_header_t) bytes into a malloc
   block, so the header size decides element alignment. 16 is checked
   explicitly because max_align_t is only 8 on Apple arm64, which hid the
   bug there, while __int128 and SIMD types need 16. */

#define CHECK_ALIGNED(p)                                                      \
    do {                                                                      \
        CHECK_EQ((uintptr_t)(p) % _Alignof(max_align_t), 0);                  \
        CHECK_EQ((uintptr_t)(p) % 16, 0);                                     \
    } while (0)

TEST(layout, header_size_preserves_alignment) {
    CHECK_EQ(sizeof(spda_header_t) % _Alignof(max_align_t), 0);
    CHECK_EQ(sizeof(spda_header_t) % 16, 0);
    CHECK(sizeof(spda_header_t) >= 3 * sizeof(size_t));
}

TEST(layout, elements_aligned_for_any_type) {
    long double *a = spda_create(long double);
    REQUIRE(a);
    CHECK_ALIGNED(a);
    spda_append(a, 1.5L);
    CHECK(a[0] == 1.5L);
    spda_destroy(a);
}

/* Alignment must survive every operation that (re)allocates. */
TEST(layout, alignment_survives_reallocation) {
    double *a = spda_create(double);
    CHECK_ALIGNED(a);
    for (int i = 0; i < 1000; i++) spda_append(a, i * 0.5);   /* many grows */
    CHECK_ALIGNED(a);
    a = _spda_resize(a, 4000);
    REQUIRE(a);
    CHECK_ALIGNED(a);
    while (spda_len(a) > 10) spda_pop(a);
    a = spda_shrink_to_fit(a);
    CHECK_ALIGNED(a);
    double *c = spda_copy(a);
    REQUIRE(c);
    CHECK_ALIGNED(c);
    spda_destroy(a);
    spda_destroy(c);
}

TEST(layout, alignment_for_every_element_size) {
    /* Odd element sizes must not shift the base: alignment comes from the
       header alone. */
    char *c1 = spda_create(char);
    Point3D *p = spda_create(Point3D);
    Big *b = spda_create(Big);
    CHECK_ALIGNED(c1);
    CHECK_ALIGNED(p);
    CHECK_ALIGNED(b);
    spda_destroy(c1);
    spda_destroy(p);
    spda_destroy(b);
}

/* A struct requiring max_align_t alignment round-trips intact. */
TEST(layout, max_align_struct_elements) {
    typedef struct { max_align_t m; char tag; } Aligned;
    Aligned *a = spda_create(Aligned);
    for (int i = 0; i < 100; i++) {
        Aligned x;
        memset(&x, 0, sizeof x);
        x.tag = (char)i;
        spda_append(a, x);
    }
    CHECK_ALIGNED(a);
    for (int i = 0; i < 100; i++) {
        REQUIRE(a[i].tag == (char)i);
        CHECK_EQ((uintptr_t)&a[i] % _Alignof(Aligned), 0);
    }
    spda_destroy(a);
}

#ifdef __SIZEOF_INT128__
/* __int128 needs 16-byte alignment even where max_align_t is 8. With the
   old 24-byte header, UBSan reports a misaligned access here. */
__extension__ typedef __int128 i128;

TEST(layout, int128_elements) {
    i128 *a = spda_create(i128);
    REQUIRE(a);
    CHECK_EQ((uintptr_t)a % _Alignof(i128), 0);
    for (int i = 0; i < 100; i++) spda_append(a, (i128)i << 70);
    for (int i = 0; i < 100; i++) REQUIRE(a[i] == ((i128)i << 70));
    spda_destroy(a);
}
#endif

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
/* Growth from zero and size overflow  (ISSUES.md B1, B2)                   */
/* ======================================================================== */
/* The overflow tests never reach malloc/realloc: under ASan an attempted
   allocation of these sizes aborts the test, so passing proves the guard
   fires before any allocation. */

/* B1: growth used to start from cap (0) and 0 * factor never terminated. */
TEST(growth, append_many_after_resize_to_zero) {
    int *a = spda_create(int);
    a = _spda_resize(a, 0);
    REQUIRE(a);
    spda_append_many(a, 1, 2, 3);
    CHECK_INTS(a, 1, 2, 3);
    CHECK(spda_cap(a) >= SPDA_DEFAULT_CAPACITY);
    CHECK_INVARIANTS(a);
    spda_destroy(a);
}

TEST(growth, append_items_large_batch_from_zero) {
    enum { N = 1000 };
    int items[N];
    for (int i = 0; i < N; i++) items[i] = i;
    int *a = spda_create(int);
    a = _spda_resize(a, 0);
    REQUIRE(a);
    spda_append_items(a, items, N);
    REQUIRE(spda_len(a) == N);
    CHECK(spda_cap(a) >= N);
    for (int i = 0; i < N; i++) REQUIRE(a[i] == i);
    spda_destroy(a);
}

/* capacity * stride wraps: must fail instead of allocating a tiny block. */
TEST(overflow, create_capacity_times_stride) {
    CHECK(spda_reserve(int, SIZE_MAX / 4 + 2) == NULL);
    CHECK(spda_reserve(Big, SIZE_MAX / 256) == NULL);
    CHECK(spda_reserve(double, SIZE_MAX) == NULL);
}

/* header + capacity * stride wraps even though the product alone fits. */
TEST(overflow, create_header_plus_elements) {
    CHECK(_spda_create(SIZE_MAX, 1) == NULL);
    CHECK(_spda_create(SIZE_MAX - 8, 1) == NULL);
}

TEST(overflow, reserve_just_below_limit_still_checks_header) {
    size_t per_int = (SIZE_MAX - sizeof(spda_header_t)) / sizeof(int);
    CHECK(spda_reserve(int, per_int + 1) == NULL);
}

/* Oversized resize fails without touching the array (length not clamped). */
TEST(overflow, resize_rejected_before_clamp) {
    int *a = make_seq(10);
    size_t cap = spda_cap(a);
    int *r = _spda_resize(a, SIZE_MAX / 2);
    CHECK(r == a);                       /* failure returns the original */
    CHECK_EQ(spda_cap(a), cap);
    CHECK_INTS(a, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    spda_destroy(a);
}

/* Doubling the capacity itself would wrap. The header is forged so no real
   allocation of that size is needed; it is restored before destroy. */
TEST(overflow, resize_def_capacity_cannot_double) {
    int *a = make_seq(3);
    size_t real_cap = spda_cap(a);
    _spda_get_header(a)->capacity = SIZE_MAX / 2 + 1;
    CHECK(_spda_resize_def(a) == a);
    CHECK_EQ(spda_cap(a), SIZE_MAX / 2 + 1);  /* capacity unchanged */
    _spda_get_header(a)->capacity = real_cap;
    CHECK_INTS(a, 0, 1, 2);
    spda_destroy(a);
}

/* Doubling fits in size_t, but doubled capacity * stride does not. */
TEST(overflow, resize_def_bytes_overflow) {
    int *a = make_seq(3);
    size_t real_cap = spda_cap(a);
    _spda_get_header(a)->capacity = SIZE_MAX / 8;
    CHECK(_spda_resize_def(a) == a);
    CHECK_EQ(spda_cap(a), SIZE_MAX / 8);      /* capacity unchanged */
    _spda_get_header(a)->capacity = real_cap;
    CHECK_INTS(a, 0, 1, 2);
    spda_destroy(a);
}

/* len + count wraps: rejected before items is ever read. */
TEST(overflow, append_many_length_plus_count) {
    int *a = make_seq(1);
    int item = 5;
    int *r = _spda_append_many(a, &item, SIZE_MAX);
    CHECK(r == a);
    CHECK_INTS(a, 0);
    spda_destroy(a);
}

/* Huge count that does not wrap len + count: the doubling loop must stop
   (jump to `required`) and the byte-size check must reject it. */
TEST(overflow, append_many_huge_count_terminates) {
    int *a = make_seq(2);
    int item = 5;
    int *r = _spda_append_many(a, &item, SIZE_MAX / 2);
    CHECK(r == a);
    CHECK_INTS(a, 0, 1);
    spda_destroy(a);
}

/* Normal growth is unaffected by the new checks. */
TEST(overflow, normal_growth_unaffected) {
    int *a = spda_create(int);
    for (int i = 0; i < 10000; i++) spda_append(a, i);
    REQUIRE(spda_len(a) == 10000);
    for (int i = 0; i < 10000; i++) REQUIRE(a[i] == i);
    CHECK_INVARIANTS(a);
    spda_destroy(a);
}

/* ======================================================================== */
/* Appending from the same array  (ISSUES.md B3)                            */
/* ======================================================================== */

/* Full array: the append must reallocate, which used to leave `items`
   pointing at the freed block (ASan: heap-use-after-free). */
TEST(self_append, whole_array_when_full) {
    int *a = make_seq(SPDA_DEFAULT_CAPACITY);
    REQUIRE(spda_len(a) == spda_cap(a));
    spda_append_items(a, a, spda_len(a));
    REQUIRE(spda_len(a) == 2 * SPDA_DEFAULT_CAPACITY);
    for (int i = 0; i < 2 * SPDA_DEFAULT_CAPACITY; i++)
        REQUIRE(a[i] == i % SPDA_DEFAULT_CAPACITY);
    CHECK_INVARIANTS(a);
    spda_destroy(a);
}

/* Room to spare: no reallocation, plain copy within the block. */
TEST(self_append, whole_array_with_spare_capacity) {
    int *a = make_seq(5);
    spda_append_items(a, a, spda_len(a));
    CHECK_INTS(a, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4);
    spda_destroy(a);
}

TEST(self_append, subrange_when_full) {
    int *a = make_seq(SPDA_DEFAULT_CAPACITY);
    spda_append_items(a, a + 5, 3);
    REQUIRE(spda_len(a) == SPDA_DEFAULT_CAPACITY + 3);
    CHECK_EQ(a[SPDA_DEFAULT_CAPACITY + 0], 5);
    CHECK_EQ(a[SPDA_DEFAULT_CAPACITY + 1], 6);
    CHECK_EQ(a[SPDA_DEFAULT_CAPACITY + 2], 7);
    spda_destroy(a);
}

/* The last live element is a valid one-element source. */
TEST(self_append, last_element) {
    int *a = make_seq(SPDA_DEFAULT_CAPACITY);
    spda_append_items(a, a + SPDA_DEFAULT_CAPACITY - 1, 1);
    REQUIRE(spda_len(a) == SPDA_DEFAULT_CAPACITY + 1);
    CHECK_EQ(a[SPDA_DEFAULT_CAPACITY], SPDA_DEFAULT_CAPACITY - 1);
    spda_destroy(a);
}

/* Doubling itself repeatedly: every step reallocates. */
TEST(self_append, repeated_doubling) {
    int *a = make_seq(3);
    for (int round = 0; round < 8; round++)
        spda_append_items(a, a, spda_len(a));
    REQUIRE(spda_len(a) == 3u << 8);
    for (size_t i = 0; i < spda_len(a); i++) REQUIRE(a[i] == (int)(i % 3));
    CHECK_INVARIANTS(a);
    spda_destroy(a);
}

TEST(self_append, structs) {
    Person *p = spda_create(Person);
    for (int i = 0; i < SPDA_DEFAULT_CAPACITY; i++)
        spda_append(p, ((Person){.id = i, .name = "x"}));
    spda_append_items(p, p, spda_len(p));
    REQUIRE(spda_len(p) == 2 * SPDA_DEFAULT_CAPACITY);
    for (int i = 0; i < 2 * SPDA_DEFAULT_CAPACITY; i++) {
        REQUIRE(p[i].id == i % SPDA_DEFAULT_CAPACITY);
        REQUIRE(strcmp(p[i].name, "x") == 0);
    }
    spda_destroy(p);
}

/* Source starts in the live elements but runs past `len`: it would read
   uninitialised slots and overlap the destination, so it is rejected. */
TEST(self_append, range_past_length_rejected) {
    int *a = make_seq(4);
    int *r = _spda_append_many(a, a + 2, 5);
    CHECK(r == a);
    CHECK_INTS(a, 0, 1, 2, 3);
    spda_destroy(a);
}

/* Source entirely in unused capacity (uninitialised): rejected. */
TEST(self_append, source_in_unused_capacity_rejected) {
    int *a = make_seq(4);
    int *r = _spda_append_many(a, a + 10, 1);
    CHECK(r == a);
    CHECK_INTS(a, 0, 1, 2, 3);
    spda_destroy(a);
}

/* A separate array right next to this one in memory is not mistaken for an
   alias: appending from another spda array still works. */
TEST(self_append, other_array_unaffected) {
    int *a = make_seq(SPDA_DEFAULT_CAPACITY);
    int *b = make_seq(3);
    spda_append_items(a, b, spda_len(b));
    REQUIRE(spda_len(a) == SPDA_DEFAULT_CAPACITY + 3);
    CHECK_EQ(a[SPDA_DEFAULT_CAPACITY], 0);
    CHECK_EQ(a[SPDA_DEFAULT_CAPACITY + 2], 2);
    spda_destroy(a);
    spda_destroy(b);
}

/* ======================================================================== */
/* Failed resize keeps the array  (ISSUES.md S1)                            */
/* ======================================================================== */
/* Contract: every function that returns the array returns the one it was
   given, unchanged, on any failure, so `a = f(a, ...)` never loses `a`.
   Out-of-memory is simulated with fail_alloc_after(). */

/* A full array, so the next append/insert must grow. */
static int *make_full(void) {
    int *a = make_seq(SPDA_DEFAULT_CAPACITY);
    return a;
}

static void check_full_untouched(const int *a) {
    CHECK_EQ(spda_len(a), SPDA_DEFAULT_CAPACITY);
    CHECK_EQ(spda_cap(a), SPDA_DEFAULT_CAPACITY);
    for (int i = 0; i < SPDA_DEFAULT_CAPACITY; i++)
        if (a[i] != i) { CHECK_EQ(a[i], i); break; }
}

TEST(oom, resize_grow_keeps_array) {
    int *a = make_seq(4);
    int *before = a;
    fail_alloc_after(0);
    a = _spda_resize(a, 1000);
    CHECK(a == before);
    CHECK_EQ(spda_cap(a), SPDA_DEFAULT_CAPACITY);
    CHECK_INTS(a, 0, 1, 2, 3);
    spda_destroy(a);
}

/* The old code clamped length before realloc: a failed shrink lost data. */
TEST(oom, resize_shrink_keeps_all_elements) {
    int *a = make_seq(10);
    int *before = a;
    fail_alloc_after(0);
    a = _spda_resize(a, 3);
    CHECK(a == before);
    CHECK_EQ(spda_cap(a), SPDA_DEFAULT_CAPACITY);
    CHECK_INTS(a, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    spda_destroy(a);
}

TEST(oom, resize_def_keeps_array) {
    int *a = make_full();
    int *before = a;
    fail_alloc_after(0);
    a = _spda_resize_def(a);
    CHECK(a == before);
    check_full_untouched(a);
    spda_destroy(a);
}

TEST(oom, shrink_to_fit_keeps_array) {
    int *a = make_seq(1000);
    while (spda_len(a) > 10) spda_pop(a);
    size_t cap = spda_cap(a);
    int *before = a;
    fail_alloc_after(0);
    a = spda_shrink_to_fit(a);
    CHECK(a == before);
    CHECK_EQ(spda_cap(a), cap);
    CHECK_INTS(a, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    spda_destroy(a);
}

TEST(oom, append_keeps_array) {
    int *a = make_full();
    int *before = a;
    fail_alloc_after(0);
    spda_append(a, 99);
    CHECK(a == before);
    check_full_untouched(a);
    spda_append(a, 99);                 /* recovers once memory is back */
    REQUIRE(spda_len(a) == SPDA_DEFAULT_CAPACITY + 1);
    CHECK_EQ(a[SPDA_DEFAULT_CAPACITY], 99);
    spda_destroy(a);
}

TEST(oom, append_many_keeps_array) {
    int *a = make_full();
    int *before = a;
    int items[100] = {0};
    fail_alloc_after(0);
    spda_append_items(a, items, 100);
    CHECK(a == before);
    check_full_untouched(a);
    spda_destroy(a);
}

/* Self-append whose resize fails: the alias offset logic must not run. */
TEST(oom, self_append_keeps_array) {
    int *a = make_full();
    int *before = a;
    fail_alloc_after(0);
    spda_append_items(a, a, spda_len(a));
    CHECK(a == before);
    check_full_untouched(a);
    spda_destroy(a);
}

TEST(oom, insert_keeps_array) {
    int *a = make_full();
    int *before = a;
    fail_alloc_after(0);
    spda_insert(a, 0, -1);
    CHECK(a == before);
    check_full_untouched(a);
    spda_destroy(a);
}

TEST(oom, create_and_copy_return_null) {
    fail_alloc_after(0);
    CHECK(spda_create(int) == NULL);
    int *a = make_seq(5);
    fail_alloc_after(0);
    CHECK(spda_copy(a) == NULL);
    CHECK_INTS(a, 0, 1, 2, 3, 4);
    spda_destroy(a);
}

/* Overflow takes the same path as OOM inside append_many: this used to
   "succeed" and memcpy past the buffer (bus error / ASan). */
TEST(oom, append_many_overflow_does_not_write) {
    int *a = make_seq(2);
    int item = 5;
    int *r = _spda_append_many(a, &item, SIZE_MAX / 2);
    CHECK(r == a);
    CHECK_INTS(a, 0, 1);
    spda_destroy(a);
}

/* ======================================================================== */
/* Header signature / validity  (ISSUES.md S3)                              */
/* ======================================================================== */

/* A fake array: a real header struct followed by element storage, but never
   created by spda. stride > 0, so the old `stride > 0` check accepted it. */
typedef struct {
    spda_header_t h;
    int data[8];
} ForgedArray;

static int *forge(ForgedArray *f, size_t magic) {
    memset(f, 0, sizeof *f);
    f->h.capacity = 8;
    f->h.length = 4;
    f->h.stride = sizeof(int);
    f->h.magic = magic;
    for (int i = 0; i < 4; i++) f->data[i] = i;
    return (int *)(&f->h + 1);
}

TEST(magic, header_size_unchanged) {
    /* The signature lives in what was alignment padding. */
    CHECK_EQ(sizeof(spda_header_t), sizeof(size_t) == 8 ? 32 : 16);
}

TEST(magic, live_arrays_are_valid) {
    int *a = make_seq(3);
    CHECK(_spda_is_valid(a));
    for (int i = 0; i < 500; i++) spda_append(a, i);   /* realloc keeps it */
    CHECK(_spda_is_valid(a));
    a = spda_shrink_to_fit(a);
    CHECK(_spda_is_valid(a));
    int *c = spda_copy(a);                             /* copy inherits it */
    REQUIRE(c);
    CHECK(_spda_is_valid(c));
    spda_destroy(a);
    spda_destroy(c);
}

TEST(magic, forged_header_rejected) {
    ForgedArray f;
    int *p = forge(&f, 0);
    CHECK(!_spda_is_valid(p));
    CHECK_EQ(spda_len(p), 0);
    CHECK_EQ(spda_cap(p), 0);
    CHECK_EQ(spda_stride(p), 0);
}

/* The forged array has room (len 4 < cap 8), so without the signature check
   these would write into it. Every call must leave it untouched. */
TEST(magic, every_operation_rejects_forged) {
    ForgedArray f;
    int *p = forge(&f, 0);
    int v = 99, out = 77;

    /* Called directly (not via the assigning macros) so this test checks the
       signature only, independent of what each function returns on failure. */
    (void)_spda_append(p, &v);
    (void)_spda_append_many(p, &v, 1);
    (void)_spda_insert(p, 0, &v);
    (void)_spda_resize(p, 100);
    (void)_spda_resize_def(p);
    (void)spda_shrink_to_fit(p);
    CHECK(!spda_remove(p, 0));
    CHECK(!spda_remove_into(p, 0, &out));
    CHECK(!spda_swap_remove(p, 0));
    CHECK(!spda_pop(p));
    CHECK(!spda_set_length(p, 2));
    spda_clear(p);
    spda_reverse(p);
    spda_sort(p, cmp_int);
    CHECK(spda_copy(p) == NULL);
    CHECK_PRINTS(spda_fprint(f, p), "NULL");
    spda_destroy(p);                     /* must not free() a stack object */

    CHECK_EQ(out, 77);
    CHECK_EQ(f.h.length, 4);
    CHECK_EQ(f.h.capacity, 8);
    for (int i = 0; i < 4; i++) CHECK_EQ(f.data[i], i);
}

TEST(magic, wrong_magic_value_rejected) {
    ForgedArray f;
    CHECK(!_spda_is_valid(forge(&f, SPDA_MAGIC ^ 1)));
    CHECK(!_spda_is_valid(forge(&f, ~SPDA_MAGIC)));
    CHECK(_spda_is_valid(forge(&f, SPDA_MAGIC)));   /* control: real signature */
}

/* Zero-filled memory in front of a pointer (e.g. a calloc'd or static
   buffer) is not an array. */
TEST(magic, zeroed_memory_rejected) {
    _Alignas(16) static unsigned char buf[256];
    int *p = (int *)(buf + 64);
    CHECK(!_spda_is_valid(p));
    spda_append(p, 1);
    CHECK_EQ(((int *)(buf + 64))[0], 0);
}

/* Detecting a double destroy has to read freed memory, which ASan reports
   by design, so this check only runs without it. */
#if defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define SPDA_TEST_ASAN 1
#  endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#  define SPDA_TEST_ASAN 1
#endif

#ifndef SPDA_TEST_ASAN
TEST(magic, destroy_poisons_signature) {
    int *a = make_seq(3);
    spda_destroy(a);
    CHECK(!_spda_is_valid(a));
    spda_destroy(a);                     /* second destroy is a no-op, not a double free */
}
#endif

/* Invalid arrays: the pointer passed in comes back, never NULL-ed. */
TEST(oom, invalid_array_pointer_preserved) {
    int *a = NULL;
    spda_insert(a, 0, 1);
    CHECK(a == NULL);
    ForgedArray f;
    int *p = forge(&f, 0), *orig = p;
    spda_insert(p, 0, 1);
    p = _spda_resize(p, 10);
    p = _spda_resize_def(p);
    p = spda_shrink_to_fit(p);
    spda_append(p, 1);
    CHECK(p == orig);
}

/* ======================================================================== */
/* Error reporting  (ISSUES.md S2)                                          */
/* ======================================================================== */

#define CHECK_ERR(expected) CHECK_EQ(spda_last_error(), (expected))

/* ---- the error API ------------------------------------------------------- */

TEST(error, strerror_covers_every_code) {
    const spda_err_t codes[] = {
        SPDA_OK, SPDA_ERR_NOMEM, SPDA_ERR_INVALID_ARRAY,
        SPDA_ERR_INVALID_ARG, SPDA_ERR_OUT_OF_BOUNDS, SPDA_ERR_OVERFLOW,
    };
    const char *unknown = spda_strerror((spda_err_t)999);
    REQUIRE(unknown != NULL);
    for (size_t i = 0; i < SPDA_COUNTOF(codes); i++) {
        const char *s = spda_strerror(codes[i]);
        REQUIRE(s != NULL);
        CHECK(s[0] != '\0');
        CHECK(strcmp(s, unknown) != 0);
        for (size_t j = 0; j < i; j++)          /* every message distinct */
            CHECK(strcmp(s, spda_strerror(codes[j])) != 0);
    }
}

TEST(error, starts_as_ok) {
    CHECK_ERR(SPDA_OK);
}

TEST(error, success_clears_previous_error) {
    int *a = make_seq(3);
    spda_remove(a, 10);
    CHECK_ERR(SPDA_ERR_OUT_OF_BOUNDS);
    spda_append(a, 3);
    CHECK_ERR(SPDA_OK);
    spda_destroy(a);
}

TEST(error, accessors_do_not_touch_error) {
    int *a = make_seq(3);
    spda_remove(a, 10);
    (void)spda_len(a);
    (void)spda_cap(a);
    (void)spda_stride(a);
    CHECK_ERR(SPDA_ERR_OUT_OF_BOUNDS);
    spda_destroy(a);
}

/* Redirect stderr into a temp file around a block of code. */
static char g_stderr_buf[4096];
static int  g_stderr_saved = -1;
static FILE *g_stderr_sink;

static void stderr_capture_begin(void) {
    fflush(stderr);
    g_stderr_sink = tmpfile();
    g_stderr_saved = dup(STDERR_FILENO);
    if (g_stderr_sink && g_stderr_saved >= 0)
        dup2(fileno(g_stderr_sink), STDERR_FILENO);
}

static const char *stderr_capture_end(void) {
    fflush(stderr);
    g_stderr_buf[0] = '\0';
    if (g_stderr_saved >= 0) {
        dup2(g_stderr_saved, STDERR_FILENO);
        close(g_stderr_saved);
        g_stderr_saved = -1;
    }
    if (g_stderr_sink) {
        rewind(g_stderr_sink);
        size_t n = fread(g_stderr_buf, 1, sizeof g_stderr_buf - 1, g_stderr_sink);
        g_stderr_buf[n] = '\0';
        fclose(g_stderr_sink);
        g_stderr_sink = NULL;
    }
    return g_stderr_buf;
}

/* Failures are logged to stderr by default: "spda: <func>: <msg> (<strerror>)". */
TEST(error, failures_logged_by_default) {
    int *a = make_seq(2);
    stderr_capture_begin();
    spda_remove(a, 5);
    const char *log = stderr_capture_end();
    CHECK(strncmp(log, "spda: ", 6) == 0);
    CHECK(strstr(log, "_spda_remove") != NULL);
    CHECK(strstr(log, "Start index out of bounds for remove.") != NULL);
    CHECK(strstr(log, "(index out of bounds)") != NULL);
    CHECK(log[0] != '\0' && log[strlen(log) - 1] == '\n');
    spda_destroy(a);
}

/* One line per failure, each naming its own error. */
TEST(error, each_failure_logged_once) {
    int *n = NULL;
    int *b = make_seq(2);
    stderr_capture_begin();
    spda_append(n, 1);
    spda_sort(b, NULL);
    fail_alloc_after(0);
    spda_append_items(b, (int[]){1}, 100);
    const char *log = stderr_capture_end();
    size_t lines = 0;
    for (const char *c = log; *c; c++) lines += (*c == '\n');
    CHECK_EQ(lines, 3);
    CHECK(strstr(log, "(invalid array)") != NULL);
    CHECK(strstr(log, "(invalid argument)") != NULL);
    CHECK(strstr(log, "(out of memory)") != NULL);
    spda_destroy(b);
}

/* Successful calls print nothing. */
TEST(error, success_logs_nothing) {
    stderr_capture_begin();
    int *a = make_seq(50);
    spda_insert(a, 0, -1);
    spda_remove(a, 0);
    spda_reverse(a);
    spda_sort(a, cmp_int);
    a = spda_shrink_to_fit(a);
    int *c = spda_copy(a);
    spda_destroy(c);
    spda_destroy(a);
    spda_destroy((int *)NULL);
    const char *log = stderr_capture_end();
    CHECK_EQ(strlen(log), 0);
}

/* ---- create / destroy ---------------------------------------------------- */

TEST(error, create_codes) {
    CHECK(_spda_create(8, 0) == NULL);
    CHECK_ERR(SPDA_ERR_INVALID_ARG);
    CHECK(spda_reserve(int, SIZE_MAX) == NULL);
    CHECK_ERR(SPDA_ERR_OVERFLOW);
    fail_alloc_after(0);
    CHECK(spda_create(int) == NULL);
    CHECK_ERR(SPDA_ERR_NOMEM);
    int *a = spda_create(int);
    REQUIRE(a);
    CHECK_ERR(SPDA_OK);
    spda_destroy(a);
}

TEST(error, destroy_codes) {
    spda_remove((int *)NULL, 0);                  /* set some error first */
    spda_destroy((int *)NULL);
    CHECK_ERR(SPDA_OK);                           /* like free(NULL) */
    ForgedArray f;
    spda_destroy(forge(&f, 0));
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);
    int *a = make_seq(2);
    spda_destroy(a);
    CHECK_ERR(SPDA_OK);
}

/* ---- append / append_many / insert --------------------------------------- */

TEST(error, append_codes) {
    int *n = NULL;
    spda_append(n, 1);
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);

    int *a = make_full();
    (void)_spda_append(a, NULL);
    CHECK_ERR(SPDA_ERR_INVALID_ARG);

    fail_alloc_after(0);
    spda_append(a, 99);
    CHECK_ERR(SPDA_ERR_NOMEM);
    check_full_untouched(a);

    spda_append(a, 99);
    CHECK_ERR(SPDA_OK);
    spda_destroy(a);
}

TEST(error, append_many_codes) {
    int *a = make_seq(2);
    int item = 5;

    a = _spda_append_many(a, NULL, 3);
    CHECK_ERR(SPDA_ERR_INVALID_ARG);
    a = _spda_append_many(a, NULL, 0);           /* nothing to add: fine */
    CHECK_ERR(SPDA_OK);

    a = _spda_append_many(a, &item, SIZE_MAX);   /* len + count wraps */
    CHECK_ERR(SPDA_ERR_OVERFLOW);
    a = _spda_append_many(a, &item, SIZE_MAX / 2); /* bytes overflow */
    CHECK_ERR(SPDA_ERR_OVERFLOW);

    a = _spda_append_many(a, a + 1, 5);          /* alias past length */
    CHECK_ERR(SPDA_ERR_INVALID_ARG);

    int items[100] = {0};
    fail_alloc_after(0);
    spda_append_items(a, items, 100);
    CHECK_ERR(SPDA_ERR_NOMEM);
    CHECK_INTS(a, 0, 1);

    spda_append_items(a, items, 100);
    CHECK_ERR(SPDA_OK);
    spda_destroy(a);
}

TEST(error, insert_codes) {
    int *n = NULL;
    spda_insert(n, 0, 1);
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);

    int *a = make_seq(3);
    (void)_spda_insert(a, 0, NULL);
    CHECK_ERR(SPDA_ERR_INVALID_ARG);
    spda_insert(a, 4, 9);
    CHECK_ERR(SPDA_ERR_OUT_OF_BOUNDS);
    spda_insert(a, 3, 9);                         /* == length is allowed */
    CHECK_ERR(SPDA_OK);
    spda_destroy(a);

    int *f = make_full();
    fail_alloc_after(0);
    spda_insert(f, 0, -1);
    CHECK_ERR(SPDA_ERR_NOMEM);
    check_full_untouched(f);
    spda_destroy(f);
}

/* ---- remove / pop / swap ------------------------------------------------- */

TEST(error, remove_codes) {
    CHECK(!spda_remove((int *)NULL, 0));
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);

    int *a = make_seq(3);
    CHECK(!spda_remove(a, 3));
    CHECK_ERR(SPDA_ERR_OUT_OF_BOUNDS);
    CHECK(!spda_remove_range(a, 1, 5));
    CHECK_ERR(SPDA_ERR_OUT_OF_BOUNDS);
    CHECK(spda_remove_range(a, 1, 0));
    CHECK_ERR(SPDA_OK);
    CHECK(!spda_swap_remove(a, 3));
    CHECK_ERR(SPDA_ERR_OUT_OF_BOUNDS);
    CHECK(spda_swap_remove(a, 0));
    CHECK_ERR(SPDA_OK);
    CHECK(spda_pop(a));
    CHECK_ERR(SPDA_OK);
    spda_destroy(a);
}

TEST(error, pop_empty) {
    int *a = spda_create(int);
    int out = 7;
    CHECK(!spda_pop(a));
    CHECK_ERR(SPDA_ERR_OUT_OF_BOUNDS);
    CHECK(!spda_pop_into(a, &out));
    CHECK_ERR(SPDA_ERR_OUT_OF_BOUNDS);
    CHECK_EQ(out, 7);
    CHECK(!spda_swap_remove(a, 0));
    CHECK_ERR(SPDA_ERR_OUT_OF_BOUNDS);
    spda_destroy(a);
}

/* ---- resize / shrink / length -------------------------------------------- */

TEST(error, resize_codes) {
    CHECK(_spda_resize(NULL, 10) == NULL);
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);
    CHECK(_spda_resize_def(NULL) == NULL);
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);
    CHECK(spda_shrink_to_fit(NULL) == NULL);
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);

    int *a = make_seq(4);
    a = _spda_resize(a, SIZE_MAX);
    CHECK_ERR(SPDA_ERR_OVERFLOW);
    fail_alloc_after(0);
    a = _spda_resize(a, 1000);
    CHECK_ERR(SPDA_ERR_NOMEM);
    fail_alloc_after(0);
    a = _spda_resize_def(a);
    CHECK_ERR(SPDA_ERR_NOMEM);

    size_t real_cap = spda_cap(a);
    _spda_get_header(a)->capacity = SIZE_MAX / 2 + 1;
    a = _spda_resize_def(a);
    CHECK_ERR(SPDA_ERR_OVERFLOW);
    _spda_get_header(a)->capacity = real_cap;

    a = _spda_resize(a, 100);
    CHECK_ERR(SPDA_OK);
    CHECK_INTS(a, 0, 1, 2, 3);
    spda_destroy(a);
}

TEST(error, shrink_codes) {
    int *a = make_seq(SPDA_DEFAULT_CAPACITY);
    a = spda_shrink_to_fit(a);                    /* nothing to do */
    CHECK_ERR(SPDA_OK);
    while (spda_len(a) > 2) spda_pop(a);
    fail_alloc_after(0);
    a = spda_shrink_to_fit(a);
    CHECK_ERR(SPDA_ERR_NOMEM);
    a = spda_shrink_to_fit(a);
    CHECK_ERR(SPDA_OK);
    spda_destroy(a);
}

TEST(error, length_codes) {
    spda_clear((int *)NULL);
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);
    int *a = make_seq(5);
    CHECK(!spda_set_length(a, spda_cap(a) + 1));
    CHECK_ERR(SPDA_ERR_OUT_OF_BOUNDS);
    CHECK_EQ(spda_len(a), 5);
    CHECK(spda_set_length(a, 2));
    CHECK_ERR(SPDA_OK);
    spda_clear(a);
    CHECK_ERR(SPDA_OK);
    spda_destroy(a);
}

/* ---- copy / reverse / sort / print --------------------------------------- */

TEST(error, copy_codes) {
    CHECK(spda_copy(NULL) == NULL);
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);
    int *a = make_seq(5);
    fail_alloc_after(0);
    CHECK(spda_copy(a) == NULL);
    CHECK_ERR(SPDA_ERR_NOMEM);
    int *c = spda_copy(a);
    REQUIRE(c);
    CHECK_ERR(SPDA_OK);
    spda_destroy(a);
    spda_destroy(c);
}

TEST(error, reverse_codes) {
    spda_reverse((int *)NULL);
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);
    int *a = make_seq(4);
    spda_reverse(a);
    CHECK_ERR(SPDA_OK);
    CHECK_INTS(a, 3, 2, 1, 0);
    spda_destroy(a);
}

/* ISSUES.md M1: reverse no longer allocates, so a failing allocator cannot
   make it fail. The pending failure is never consumed, so reset it. */
TEST(error, reverse_never_allocates) {
    int *a = make_seq(4);
    fail_alloc_after(0);
    spda_reverse(a);
    CHECK_ERR(SPDA_OK);
    CHECK_INTS(a, 3, 2, 1, 0);
    CHECK_EQ(g_alloc_fail_in, 0);          /* still armed: nothing allocated */
    fail_alloc_after(-1);
    spda_destroy(a);
}

TEST(error, sort_codes) {
    spda_sort((int *)NULL, cmp_int);
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);
    int *a = make_seq(3);
    spda_reverse(a);
    spda_sort(a, NULL);
    CHECK_ERR(SPDA_ERR_INVALID_ARG);
    CHECK_INTS(a, 2, 1, 0);
    spda_sort(a, cmp_int);
    CHECK_ERR(SPDA_OK);
    spda_destroy(a);
}

TEST(error, print_codes) {
    int *a = make_seq(2);
    spda_fprint(NULL, a);
    CHECK_ERR(SPDA_ERR_INVALID_ARG);
    CHECK_PRINTS(spda_fprint_with(f, a, NULL), "");
    CHECK_ERR(SPDA_ERR_INVALID_ARG);
    int *n = NULL;
    CHECK_PRINTS(spda_fprint(f, n), "NULL");
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);
    CHECK_PRINTS(spda_fprint(f, a), "[0, 1]");
    CHECK_ERR(SPDA_OK);
    spda_destroy(a);
}

/* Forged and destroyed arrays report INVALID_ARRAY rather than a misleading
   OUT_OF_BOUNDS from the zero length the accessors return. */
TEST(error, forged_array_reports_invalid) {
    ForgedArray f;
    int *p = forge(&f, 0);
    CHECK(!spda_remove(p, 0));
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);
    (void)_spda_append(p, &(int){1});
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);
}

/* ======================================================================== */
/* Macro arguments evaluated once  (ISSUES.md M2)                           */
/* ======================================================================== */
/* Each test passes an argument with a side effect (arrs[i++], a counting
   function) and checks it ran exactly once and hit the intended array. */

static int g_pick_calls;
static int **pick_slot(int **slot) { g_pick_calls++; return slot; }
static int  count_int(int v)       { g_pick_calls++; return v; }
static size_t count_idx(size_t v)  { g_pick_calls++; return v; }

TEST(macro, append_evaluates_array_once) {
    int *arrs[2] = { spda_create(int), spda_create(int) };
    int i = 0;
    spda_append(arrs[i++], 5);
    CHECK_EQ(i, 1);
    CHECK_INTS(arrs[0], 5);
    CHECK_EQ(spda_len(arrs[1]), 0);            /* neighbour untouched */
    spda_destroy(arrs[0]);
    spda_destroy(arrs[1]);
}

/* A full array reallocates: the moved pointer must land in the same slot. */
TEST(macro, append_realloc_writes_back_to_same_slot) {
    int *arrs[2] = { make_full(), spda_create(int) };
    int *other = arrs[1];
    int i = 0;
    spda_append(arrs[i++], 99);
    CHECK_EQ(i, 1);
    REQUIRE(spda_len(arrs[0]) == SPDA_DEFAULT_CAPACITY + 1);
    CHECK_EQ(arrs[0][SPDA_DEFAULT_CAPACITY], 99);
    CHECK(arrs[1] == other);
    spda_destroy(arrs[0]);
    spda_destroy(arrs[1]);
}

TEST(macro, append_through_function_call) {
    int *a = spda_create(int);
    g_pick_calls = 0;
    spda_append(*pick_slot(&a), 7);
    CHECK_EQ(g_pick_calls, 1);
    CHECK_INTS(a, 7);
    spda_destroy(a);
}

TEST(macro, append_value_evaluated_once) {
    int *a = spda_create(int);
    g_pick_calls = 0;
    spda_append(a, count_int(42));
    CHECK_EQ(g_pick_calls, 1);
    CHECK_INTS(a, 42);
    spda_destroy(a);
}

TEST(macro, insert_evaluates_each_argument_once) {
    int *arrs[2] = { make_full(), spda_create(int) };
    int i = 0;
    g_pick_calls = 0;
    spda_insert(arrs[i++], count_idx(0), count_int(-1));
    CHECK_EQ(i, 1);
    CHECK_EQ(g_pick_calls, 2);                 /* idx once, value once */
    REQUIRE(spda_len(arrs[0]) == SPDA_DEFAULT_CAPACITY + 1);
    CHECK_EQ(arrs[0][0], -1);
    CHECK_EQ(arrs[0][1], 0);
    CHECK_EQ(spda_len(arrs[1]), 0);
    spda_destroy(arrs[0]);
    spda_destroy(arrs[1]);
}

TEST(macro, append_many_evaluates_array_once) {
    int *arrs[2] = { make_full(), spda_create(int) };
    int i = 0;
    spda_append_many(arrs[i++], 100, 101, 102);
    CHECK_EQ(i, 1);
    REQUIRE(spda_len(arrs[0]) == SPDA_DEFAULT_CAPACITY + 3);
    CHECK_EQ(arrs[0][SPDA_DEFAULT_CAPACITY + 2], 102);
    CHECK_EQ(spda_len(arrs[1]), 0);
    spda_destroy(arrs[0]);
    spda_destroy(arrs[1]);
}

TEST(macro, append_items_evaluates_array_once) {
    int items[3] = {7, 8, 9};
    int *arrs[2] = { make_full(), spda_create(int) };
    int i = 0;
    spda_append_items(arrs[i++], items, 3);
    CHECK_EQ(i, 1);
    REQUIRE(spda_len(arrs[0]) == SPDA_DEFAULT_CAPACITY + 3);
    CHECK_EQ(arrs[0][SPDA_DEFAULT_CAPACITY], 7);
    CHECK_EQ(spda_len(arrs[1]), 0);
    spda_destroy(arrs[0]);
    spda_destroy(arrs[1]);
}

TEST(macro, pop_evaluates_array_once) {
    int *arrs[2] = { make_seq(3), make_seq(3) };
    int i = 0, out = -1;
    CHECK(spda_pop(arrs[i++]));
    CHECK_EQ(i, 1);
    CHECK(spda_pop_into(arrs[--i], &out));     /* back to slot 0 */
    CHECK_EQ(i, 0);
    CHECK_EQ(out, 1);
    CHECK_INTS(arrs[0], 0);
    CHECK_INTS(arrs[1], 0, 1, 2);
    spda_destroy(arrs[0]);
    spda_destroy(arrs[1]);
}

TEST(macro, pop_empty_reports_pop) {
    int *a = spda_create(int);
    stderr_capture_begin();
    CHECK(!spda_pop(a));
    const char *log = stderr_capture_end();
    CHECK_ERR(SPDA_ERR_OUT_OF_BOUNDS);
    CHECK(strstr(log, "_spda_pop") != NULL);
    CHECK(strstr(log, "Cannot pop from an empty array.") != NULL);
    spda_destroy(a);
}

TEST(macro, pop_null_reports_invalid_array) {
    int out = 7;
    CHECK(!spda_pop_into((int *)NULL, &out));
    CHECK_ERR(SPDA_ERR_INVALID_ARRAY);
    CHECK_EQ(out, 7);
}

TEST(macro, foreach_evaluates_array_once) {
    int *a = make_seq(10);
    int **slot = &a;
    int sum = 0, visits = 0;
    g_pick_calls = 0;
    spda_foreach(int, it, *pick_slot(slot)) {
        sum += *it;
        visits++;
    }
    CHECK_EQ(g_pick_calls, 1);
    CHECK_EQ(visits, 10);
    CHECK_EQ(sum, 45);
    spda_destroy(a);
}

TEST(macro, foreach_nested_loops) {
    int *a = make_seq(3);
    int pairs = 0;
    spda_foreach(int, x, a)
        spda_foreach(int, y, a)
            pairs += (*x == *y);
    CHECK_EQ(pairs, 3);
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
