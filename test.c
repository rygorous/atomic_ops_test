#include <stdio.h>
#include <stdint.h>
#include <windows.h>

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
    T(other_core_write_line) \
    T(three_cores_read_line) \
    T(three_cores_write_line)

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
enum { NUM_INTERFERENCE_THREADS = 7 };

typedef struct {
    int core_id;
    int interference_mode;
} thread_args;

// Scratch area. This is where our memory updates go to.
// Cache-line aligned (on x86-64).
static __declspec(align(64)) uint64_t scratch[16];

// We use this event to signal to the interference thread that it's time to exit.
static HANDLE exit_event;

// Number of threads that have started running the interference main loop.
static volatile LONG num_running;

static unsigned int __stdcall interference_thread(void *argp)
{
    const thread_args *args = (const thread_args *)argp;
    uint64_t private_mem[8] = { 0 };
    uint64_t *interfere_ptr = private_mem;
    int do_writes = 0;
    int just_started = 1;

    lock_to_logical_core(args->core_id);

    switch (args->interference_mode) {
    case IM_hyperthread_running:
        if (args->core_id == 1)
            interfere_ptr = private_mem;
        break;

    case IM_hyperthread_read_line:
        if (args->core_id == 1)
            interfere_ptr = scratch;
        break;

    case IM_hyperthread_write_line:
        if (args->core_id == 1) {
            interfere_ptr = scratch;
            do_writes = 1;
        }
        break;

    case IM_other_core_read_line:
        if (args->core_id == 2)
            interfere_ptr = scratch;
        break;

    case IM_other_core_write_line:
        if (args->core_id == 2) {
            interfere_ptr = scratch;
            do_writes = 1;
        }
        break;

    case IM_three_cores_read_line:
        if (args->core_id == 2 || args->core_id == 4 || args->core_id == 6)
            interfere_ptr = scratch;
        break;

    case IM_three_cores_write_line:
        if (args->core_id == 2 || args->core_id == 4 || args->core_id == 6) {
            interfere_ptr = scratch;
            do_writes = 1;
        }
        break;
    }

    // main loop
    while (WaitForSingleObject(exit_event, 0) == WAIT_TIMEOUT) {
        if (!do_writes)
            interference_read(interfere_ptr);
        else
            interference_write(interfere_ptr);

        // after first loop through (so everything is warmed up),
        // this thread counts as "running".
        if (just_started) {
            just_started = 0;
            InterlockedIncrement(&num_running);
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

    exit_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    lock_to_logical_core(0);

    for (interference_mode = 0; interference_mode < IM_count; interference_mode++) {
        thread_args args[NUM_INTERFERENCE_THREADS];
        HANDLE threads[NUM_INTERFERENCE_THREADS];
        int i;

        printf("interference type: %s\n", interference_name(interference_mode));

        // start the interference threads
        num_running = 0;
        for (i = 0; i < NUM_INTERFERENCE_THREADS; i++) {
            args[i].core_id = i + 1;
            args[i].interference_mode = interference_mode;
            threads[i] = (HANDLE) _beginthreadex(NULL, 0, interference_thread, &args[i], 0, NULL);
        }

        // wait until they're all running (yeah, evil spin loop)
        while (num_running < NUM_INTERFERENCE_THREADS)
            Sleep(0);
        
        // run our tests
        #define T(id) printf("%16s: %8.2f cycles/op\n", #id, (double) run_test(test_##id) / 40000.0);
        TESTS
        #undef T

        // wait for thread to shut down
        SetEvent(exit_event);
        WaitForMultipleObjects(NUM_INTERFERENCE_THREADS, threads, TRUE, INFINITE);
        ResetEvent(exit_event);
        for (i = 0; i < NUM_INTERFERENCE_THREADS; i++)
            CloseHandle(threads[i]);
    }

    CloseHandle(exit_event);

    return 0;
}
