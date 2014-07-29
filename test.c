#include <stdio.h>
#include <stdint.h>
#include <windows.h>

// Scratch area. This is where our memory updates go to.
// Cache-line aligned (on x86-64).
static __declspec(align(64)) uint64_t scratch[16];

// We use this event to signal to the interference thread that it's time to exit.
static HANDLE running_event, exit_event;

// The list of tests.
#define TESTS \
    T(add) \
    T(add_mfence) \
    T(lockadd) \
    T(xadd) \
    T(swap) \
    T(cmpxchg) \
    T(lockadd_unalign)

// The list of interference modes
#define INTERFERENCE_MODES \
    T(none) \
    T(hyperthread_running) \
    T(hyperthread_read_line) \
    T(hyperthread_write_line) \
    T(other_core_read_line) \
    T(other_core_write_line)

// Declare the tests
#define T(id) extern int64_t test_##id(uint64_t *mem);
TESTS
#undef T

extern void interference_read(uint64_t *mem);
extern void interference_write(uint64_t *mem);

// Declare interference modes
enum
{
#define T(id) IM_##id,
    INTERFERENCE_MODES
    IM_count
#undef T
};

static const char *interference_name(int which)
{
    static const char *names[] = {
    #define T(id) #id,
        INTERFERENCE_MODES
    #undef T
    };
    return names[which];
}

static void lock_to_logical_core(uint32_t which)
{
    SetThreadAffinityMask(GetCurrentThread(), 1 << which);
}

// Interference thread

static unsigned int __stdcall interference_thread(void *argp)
{
    uint64_t private_mem[8] = { 0 };
    uint64_t *interfere_ptr = 0;
    int do_writes = 0;
    int just_started = 1;

    int arg = *(int *)argp;
    switch (arg) {
    case IM_none:
        interfere_ptr = private_mem;
        lock_to_logical_core(2);
        break;

    case IM_hyperthread_running:
        interfere_ptr = private_mem;
        lock_to_logical_core(1);
        break;

    case IM_hyperthread_read_line:
        interfere_ptr = scratch;
        lock_to_logical_core(1);
        break;

    case IM_hyperthread_write_line:
        interfere_ptr = scratch;
        do_writes = 1;
        lock_to_logical_core(1);
        break;

    case IM_other_core_read_line:
        interfere_ptr = scratch;
        lock_to_logical_core(2);
        break;

    case IM_other_core_write_line:
        interfere_ptr = scratch;
        do_writes = 1;
        lock_to_logical_core(2);
        break;
    }

    // main loop
    while (WaitForSingleObject(exit_event, 0) == WAIT_TIMEOUT) {
        if (!do_writes)
            interference_read(interfere_ptr);
        else
            interference_write(interfere_ptr);

        // after first loop through (so everything is warmed up),
        // set "running_event" if we're freshly started.
        if (just_started) {
            SetEvent(running_event);
            just_started = 0;
        }
    }

    return 0;
}

static int compare_results(const void *a, const void *b)
{
    int64_t ia = *(const int64_t *)a;
    int64_t ib = *(const int64_t *)b;
    if (ia != ib)
        return (ia < ib) ? -1 : 1;
    else
        return 0;
}

static int64_t run_test(int64_t (*test_kernel)(uint64_t *mem))
{
    // run a lot of times and report the median
    enum { NUM_RUNS = 100 };
    int64_t result[NUM_RUNS];
    int run;

    for (run = 0; run < NUM_RUNS; run++)
        result[run] = test_kernel(scratch);

    qsort(result, NUM_RUNS, sizeof(result[0]), compare_results);
    return result[NUM_RUNS / 2];
}

int main()
{
    int interference_mode;

    running_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    exit_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    lock_to_logical_core(0);

    for (interference_mode = 0; interference_mode < IM_count; interference_mode++) {
        HANDLE thread;

        printf("interference type: %s\n", interference_name(interference_mode));
        thread = (HANDLE) _beginthreadex(NULL, 0, interference_thread, &interference_mode, 0, NULL);
        
        // wait for thread to start running
        WaitForSingleObject(running_event, INFINITE);

        // run our tests
        #define T(id) printf("%16s: %7.2f cycles/op\n", #id, (double) run_test(test_##id) / 40000.0);
        TESTS
        #undef T

        // wait for thread to shut down
        SetEvent(exit_event);
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }

    CloseHandle(running_event);
    CloseHandle(exit_event);

    return 0;
}
