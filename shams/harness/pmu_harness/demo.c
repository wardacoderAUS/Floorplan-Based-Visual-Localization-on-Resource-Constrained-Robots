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
static void read_random(void) { uint32_t x = 1, s = 0;
    for (size_t i = 0; i < words / 16; i++) { x = x * 1664525u + 1013904223u; s += buf[x % words]; }
    sink = s; }

int demo_main(void) {
    if (pmu_init() != 0) return 1;
    size_t bytes; buf = pmu_big_buffer(&bytes);
    if (!buf) { printf("no memory for the test buffer\n"); return 1; }
    words = bytes / 4;
    for (size_t i = 0; i < words; i++) buf[i] = (uint32_t)i;

    const char *names[] = { "wait", "compute", "seq-mem", "rand-mem" };
    void (*stage[])(void) = { wait_2ms, arithmetic, read_in_order, read_random };
    for (int f = 0; f < 50; f++)                                  /* 50 frames of 4 stages */
        for (int s = 0; s < 4; s++) { pmu_begin(s); stage[s](); pmu_end(s); }

    pmu_report(names, 4);
    printf("expected:  wait=WAIT  compute=COMPUTE  seq-mem=MEMORY  rand-mem=MEMORY\n");
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
