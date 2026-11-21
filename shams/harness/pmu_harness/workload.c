/* workload.c - an ordinary multi-threaded program used to validate the harness.
 * It contains NO harness calls: the harness must find its threads and explain them by itself.
 * Each thread has one known personality, so the correct label is known in advance:
 *
 *   thread      what it does                                        expected label
 *   compute     floating-point arithmetic in registers               COMPUTE
 *   rand-mem    reads a large buffer at random positions             MEMORY (last level)
 *   seq-mem     reads the same buffer in order                       MEMORY (low exposed, high hidden)
 *   sleeper     sleeps 2 ms, then does a little work, 50 times       BLOCKED
 *   shared1..3  the compute work, all three forced onto ONE core     PREEMPTED (about 2/3 each)
 *   main        starts the threads and waits for them                BLOCKED
 *
 * Linux: ./pmurun ./workload     ESP32-P4: see mcu_main.c     GAP9: runs the parts in turn */
#if defined(__linux__)
#  define _GNU_SOURCE                                 /* thread names and core pinning */
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#if defined(ESP_PLATFORM)
#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"
#  include "esp_cpu.h"
#  define SCALE 20                                   /* a microcontroller: 20x less work */
#  define BUF_BYTES (4u << 20)                        /* PSRAM                            */
#elif defined(__linux__)
#  include <pthread.h>
#  include <sched.h>
#  include <unistd.h>
#  define SCALE 1
#  define BUF_BYTES (64u << 20)                       /* far larger than any cache        */
#else                                                 /* GAP9: one thread, parts in turn  */
#  include "pmsis.h"
#  define SCALE 50
#  define BUF_BYTES (256u << 10)
#endif

static volatile uint64_t sink;
static uint32_t *buf; static size_t words;

static void compute(void) {                           /* 4 independent float chains */
    float a = 1, b = 2, c = 3, d = 4;
    for (long i = 0; i < 40000000L / SCALE; i++) {
        a = a * 0.9999f + 1e-4f; b = b * 0.9999f + 1e-4f; c = c * 0.9999f + 1e-4f; d = d * 0.9999f + 1e-4f;
    }
    sink = (uint64_t)(a + b + c + d);
}
static void rand_mem(void) {                          /* random reads: latency */
    uint32_t x = 1, s = 0;
    for (long i = 0; i < 8000000L / SCALE; i++) { x = x * 1664525u + 1013904223u; s += buf[x % words]; }
    sink = s;
}
static void seq_mem(void) {                           /* in-order reads: bandwidth */
    uint32_t s = 0;
    for (int r = 0; r < 8; r++) for (size_t i = 0; i < words; i++) s += buf[i];
    sink = s;
}
static void sleep_ms(int ms);
static void sleeper(void) {                           /* mostly asleep */
    for (int r = 0; r < 50; r++) {
        sleep_ms(2);
        uint32_t s = 0; for (int i = 0; i < 10000; i++) s += (uint32_t)i * r; sink = s;
    }
}

/* ---- the only platform-specific part: starting threads ---- */
#if defined(__linux__)
static void sleep_ms(int ms) { usleep(ms * 1000); }
typedef struct { const char *name; void (*fn)(void); int pin; } job_t;
static void *run_job(void *a) {
    job_t *j = a;
    pthread_setname_np(pthread_self(), j->name);
    if (j->pin >= 0) {                                /* the shared threads: all on one core */
        cpu_set_t one; CPU_ZERO(&one); CPU_SET(j->pin, &one);
        pthread_setaffinity_np(pthread_self(), sizeof one, &one);
    }
    j->fn();
    return NULL;
}
static void run_all(job_t *jobs, int n) {
    pthread_t th[16];
    for (int i = 0; i < n; i++) pthread_create(&th[i], NULL, run_job, &jobs[i]);
    for (int i = 0; i < n; i++) pthread_join(th[i], NULL);
}
#elif defined(ESP_PLATFORM)
static void sleep_ms(int ms) { vTaskDelay(pdMS_TO_TICKS(ms) ? pdMS_TO_TICKS(ms) : 1); }
typedef struct { const char *name; void (*fn)(void); int pin; } job_t;
static volatile int running;
static void run_job(void *a) { job_t *j = a; j->fn(); running--; vTaskDelete(NULL); }
static void run_all(job_t *jobs, int n) {
    int core = esp_cpu_get_core_id();
    for (int i = 0; i < n; i++) {                     /* all on this core: they share it */
        running++;
        xTaskCreatePinnedToCore(run_job, jobs[i].name, 4096, &jobs[i], uxTaskPriorityGet(NULL), NULL, core);
    }
    while (running > 0) vTaskDelay(1);
}
#else
static void sleep_ms(int ms) { pi_time_wait_us(ms * 1000); }
typedef struct { const char *name; void (*fn)(void); int pin; } job_t;
static void run_all(job_t *jobs, int n) { for (int i = 0; i < n; i++) jobs[i].fn(); }
#endif

int workload_main(void) {
    words = BUF_BYTES / 4;
    buf = malloc(BUF_BYTES);
    if (!buf) { printf("workload: no memory\n"); return 1; }
    for (size_t i = 0; i < words; i++) buf[i] = (uint32_t)i;
    int pin = 0;
#if defined(__linux__)
    pin = sched_getcpu();                             /* the shared threads go to this core */
#endif
    job_t jobs[] = {
        { "compute", compute, -1 }, { "rand-mem", rand_mem, -1 }, { "seq-mem", seq_mem, -1 },
        { "sleeper", sleeper, -1 },
        { "shared1", compute, pin }, { "shared2", compute, pin }, { "shared3", compute, pin },
    };
    run_all(jobs, (int)(sizeof jobs / sizeof jobs[0]));
    free(buf);
    return 0;
}

#if defined(__linux__)
int main(void) { return workload_main(); }            /* an ordinary program */
#endif
