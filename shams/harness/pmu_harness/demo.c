/* demo.c - check the method on stages whose answer we know. The stages do NOT
 * tell the harness what they do: it only sees counters and clocks.
 * Same file on every board. */
#include "pmu.h"
#include <stdint.h>
#include <stdio.h>

static volatile uint64_t sink;
static uint32_t *buf; static size_t words;

static void wait_2ms(void)   { pmu_sleep_us(2000); }
static void arithmetic(void) { float a = 1, b = 2, c = 3, d = 4;
    for (int i = 0; i < 200000; i++) { a = a * 0.9999f + 1e-4f; b = b * 0.9999f + 1e-4f; c = c * 0.9999f + 1e-4f; d = d * 0.9999f + 1e-4f; }
    sink = (uint64_t)(a + b + c + d); }
static void read_in_order(void) { uint32_t s = 0;
    for (size_t i = 0; i < words; i++) s += buf[i];
    sink = s; }
static void shared_cpu(void) { for (int r = 0; r < 64; r++) arithmetic(); }   /* long enough to be switched out */
static void read_random(void) { uint32_t x = 1, s = 0;
    for (size_t i = 0; i < words / 16; i++) { x = x * 1664525u + 1013904223u; s += buf[x % words]; }
    sink = s; }

/* a second and third thread for shared-cpu: each one is measured too (stage 5) */
static volatile int competing;
static void competitor(int id) { (void)id; while (competing) { pmu_begin(5); arithmetic(); pmu_end(5); } }

int demo_main(void) {
    if (pmu_init() != 0) return 1;
    size_t bytes; buf = pmu_big_buffer(&bytes);
    if (!buf) { printf("no memory for the test buffer\n"); return 1; }
    words = bytes / 4;
    for (size_t i = 0; i < words; i++) buf[i] = (uint32_t)i;

    const char *names[] = { "wait", "compute", "seq-mem", "rand-mem", "shared-cpu", "comp-work" };
    void (*stage[])(void) = { wait_2ms, arithmetic, read_in_order, read_random, shared_cpu };
    int ns = 5;
    for (int f = 0; f < 50; f++)                                  /* 50 frames of the stages */
        for (int s = 0; s < ns; s++) {
            /* shared-cpu: the same arithmetic, while two competitor threads run on this core,
               so the scheduler switches between three (started outside the measurement) */
            if (s == 4) { competing = 1; if (pmu_spawn(2, "comp", competitor) == 0) { ns = 4; break; } }
            pmu_begin(s); stage[s](); pmu_end(s);
            if (s == 4) { competing = 0; pmu_join(); }
        }

    pmu_report(names, 6);
    printf("\nexpected:  wait=WAIT  compute=COMPUTE  seq-mem=MEMORY  rand-mem=MEMORY%s\n",
           ns == 5 ? "  shared-cpu=WAIT (main, comp1, comp2 about 1/3 each)" : "");
    return 0;
}

/* ---- entry point for each platform ---- */
#if defined(ESP_PLATFORM)
void app_main(void) { demo_main(); }
#elif defined(__has_include) && __has_include("pmsis.h")
#include "pmsis.h"
static void entry(void) { demo_main(); pmsis_exit(0); }
int main(void) { return pmsis_kickoff((void *)entry); }
#else
int main(void) { return demo_main(); }
#endif
