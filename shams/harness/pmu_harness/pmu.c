/* pmu.c - see pmu.h.
 *
 * PART A (platform layer) is the only part that differs per board. It answers:
 *   - which counters exist, and how to read them
 *   - the clocks
 *   - which kinds of memory the board has, and how to allocate in each
 * PART B (the method) is identical on every board:
 *   1. CALIBRATE  measure this board's prices: instructions per cycle, cost of a miss
 *   2. TIERS      measure how slow each kind/size of memory is (timing only)
 *   3. MEASURE    read the counters at the start and end of each stage
 *   4. EXPLAIN    counted work x prices -> compute / memory / other / waiting      */
#define _GNU_SOURCE
#include "pmu.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Four generic counters. Each is optional; have[k] says if this board has it.
 *   CYC  cycles      INS  instructions finished
 *   NEAR data that was not in the fastest memory (first-level miss)
 *   FAR  data that came from the slowest memory (main memory / PSRAM)       */
enum { CYC, INS, NEAR, FAR, NC };
static int have[NC];
static int have_cpu_clock;                 /* can we tell running from waiting? */
typedef struct { const char *name; int caps; size_t max; } region_t;

/* ============================== PART A: platforms ============================== */

#if defined(ESP_PLATFORM)                                   /* ---- ESP32, ESP32-P4 ---- */
#include "esp_cpu.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#if defined(__has_include)
#  if __has_include("esp_cache_cnt.h")                       /* ESP-IDF with cache counters */
#    include "esp_cache_cnt.h"
#    define PMU_ESP_CACHE_CNT 1
#  endif
#endif
#define PMU_STEPS 200000                                    /* shorter loops on a microcontroller */
#define WRAP32 1                                            /* counters are 32-bit                */
static region_t regions[] = {
#ifdef MALLOC_CAP_TCM
    { "TCM",      MALLOC_CAP_TCM, 8 << 10 },
#endif
    { "internal", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, 256 << 10 },
    { "PSRAM",    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, 4 << 20 },
};
static void *region_alloc(const region_t *r, size_t n) { return heap_caps_aligned_alloc(64, n, r->caps); }
static void  region_free(const region_t *r, void *p, size_t n) { (void)r; (void)n; heap_caps_free(p); }
static uint64_t now_ns(void) { return (uint64_t)esp_timer_get_time() * 1000; }
static uint64_t cpu_ns(void) { return now_ns(); }           /* no per-task CPU clock: waiting shows as STALLED */
void pmu_sleep_us(unsigned us) {                          /* at least one RTOS tick (set FREERTOS_HZ=1000) */
    TickType_t t = pdMS_TO_TICKS((us + 999) / 1000); vTaskDelay(t ? t : 1); }
static void counters_open(void) {
    have[CYC] = 1;                                          /* every ESP32 counts cycles */
#if defined(__riscv)
    have[INS] = 1;                                          /* RISC-V instret (Zicntr); see note in README */
#endif
#ifdef PMU_ESP_CACHE_CNT
    if (esp_cache_cnt_num_units() > 0 && esp_cache_cnt_start() == ESP_OK) have[NEAR] = have[FAR] = 1;
#endif
}
static void counters_read(uint64_t v[NC]) {
    memset(v, 0, NC * sizeof v[0]);
    v[CYC] = esp_cpu_get_cycle_count();
#if defined(__riscv)
    uint32_t ins; __asm__ volatile("rdinstret %0" : "=r"(ins)); v[INS] = ins;
#endif
#ifdef PMU_ESP_CACHE_CNT
    int core = esp_cpu_get_core_id();
    for (size_t u = 0; u < esp_cache_cnt_num_units(); u++) {   /* sum line fills (true misses) per level */
        esp_cache_cnt_unit_info_t info; esp_cache_cnt_data_t d;
        if (esp_cache_cnt_get_unit_info(u, &info) != ESP_OK || esp_cache_cnt_get(u, &d) != ESP_OK) continue;
        if (!(d.valid_mask & ESP_CACHE_CNT_VALID_LINE_FILLS)) continue;
        if (info.core_id != core && info.core_id != -1) continue;
        v[info.cache_level <= 1 ? NEAR : FAR] += d.line_fills;
    }
#endif
}

#elif defined(__has_include) && __has_include("pmsis.h")      /* ---- GAP9 (PMSIS) ---- */
#include "pmsis.h"
#define PMU_STEPS 200000
#define WRAP32 1
static region_t regions[] = { { "L2", 0, 512 << 10 } };     /* main core (FC) sees L2 directly */
static void *region_alloc(const region_t *r, size_t n) { (void)r; return pi_l2_malloc(n); }
static void  region_free(const region_t *r, void *p, size_t n) { (void)r; pi_l2_free(p, n); }
static uint64_t now_ns(void) { return (uint64_t)pi_time_get_us() * 1000; }
static uint64_t cpu_ns(void) { return now_ns(); }
void pmu_sleep_us(unsigned us) { pi_time_wait_us(us); }
static void counters_open(void) {                           /* real chip: cycles + ONE other event */
    pi_perf_conf((1 << PI_PERF_CYCLES) | (1 << PI_PERF_INSTR));
    pi_perf_reset(); pi_perf_start();
    have[CYC] = have[INS] = 1;
}
static void counters_read(uint64_t v[NC]) {
    memset(v, 0, NC * sizeof v[0]);
    v[CYC] = pi_perf_read(PI_PERF_CYCLES);
    v[INS] = pi_perf_read(PI_PERF_INSTR);
}

#elif defined(__linux__)                                    /* ---- Jetson, Raspberry Pi, Linux PC ---- */
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#define PMU_STEPS 2000000
#define WRAP32 0
static region_t regions[] = { { "RAM", 0, 64 << 20 } };     /* caches are found by size, not by place */
static void *region_alloc(const region_t *r, size_t n) { (void)r; return aligned_alloc(64, n); }
static void  region_free(const region_t *r, void *p, size_t n) { (void)r; (void)n; free(p); }
static uint64_t clk(clockid_t id) { struct timespec t; clock_gettime(id, &t); return t.tv_sec * 1000000000ull + t.tv_nsec; }
static uint64_t now_ns(void) { return clk(CLOCK_MONOTONIC_RAW); }
static uint64_t cpu_ns(void) { return clk(CLOCK_THREAD_CPUTIME_ID); }   /* only while this thread runs */
void pmu_sleep_us(unsigned us) { usleep(us); }
static int fd[NC];
static int perf_open(uint32_t type, uint64_t config) {
    struct perf_event_attr a;
    memset(&a, 0, sizeof a);
    a.size = sizeof a; a.type = type; a.config = config;
    a.exclude_kernel = 1; a.exclude_hv = 1;                       /* count only our own code */
    return (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);   /* 0 = this thread         */
}
#define READ_MISS(c) ((c) | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16))
static void counters_open(void) {
    struct { int k; uint32_t type; uint64_t cfg; } tries[] = {   /* generic names first, then chip-specific */
        { CYC,  PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES },
        { INS,  PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS },
        { NEAR, PERF_TYPE_HW_CACHE, READ_MISS(PERF_COUNT_HW_CACHE_L1D) },
        { FAR,  PERF_TYPE_HW_CACHE, READ_MISS(PERF_COUNT_HW_CACHE_LL) },
#if defined(__aarch64__)
        { FAR,  PERF_TYPE_RAW, 0x17 },          /* ARM L2D_CACHE_REFILL: last level on Cortex-A57 (Jetson Nano) */
#else
        { FAR,  PERF_TYPE_HARDWARE, PERF_COUNT_HW_CACHE_MISSES },
#endif
    };
    for (size_t i = 0; i < sizeof tries / sizeof tries[0]; i++) {
        int k = tries[i].k;
        if (have[k]) continue;
        int f = perf_open(tries[i].type, tries[i].cfg);
        if (f >= 0) { fd[k] = f; have[k] = 1; }
    }
    have_cpu_clock = 1;
}
static void counters_read(uint64_t v[NC]) {
    for (int k = 0; k < NC; k++) if (!have[k] || read(fd[k], &v[k], 8) != 8) v[k] = 0;
}

#else
#error "pmu.c: unknown platform (supported: Linux, ESP-IDF, GAP9/PMSIS)"
#endif

#define NREG (int)(sizeof regions / sizeof regions[0])

/* ============================== PART B: the method ============================== */

typedef struct { uint64_t c[NC], cpu, wall; } snap_t;
typedef struct { double c[NC], cpu, wall, run; } delta_t;
static void take(snap_t *s) { counters_read(s->c); s->cpu = cpu_ns(); s->wall = now_ns(); }
static delta_t sub(const snap_t *a, const snap_t *b) {
    delta_t d;
    for (int k = 0; k < NC; k++) d.c[k] = WRAP32 ? (double)(uint32_t)(b->c[k] - a->c[k]) : (double)(b->c[k] - a->c[k]);
    d.cpu = (double)(b->cpu - a->cpu); d.wall = (double)(b->wall - a->wall);
    d.run = have[CYC] ? d.c[CYC] : d.cpu;          /* running time: cycles if counted, else ns */
    return d;
}

/* ---- 1. CALIBRATE ---- */
static double peak_ipc;      /* most instructions per cycle this core can finish     */
static double cost[NC];      /* extra cycles that one NEAR / one FAR miss costs      */
static volatile uint64_t sink;

static void busy_loop(long n) {     /* 8 independent adds: no memory, nothing to wait for */
    uint32_t a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0;
    for (long i = 0; i < n; i++) {
        a += i; b += i; c += i; d += i; e += i; f += i; g += i; h += i;
        __asm__ volatile("" : "+r"(a), "+r"(b), "+r"(c), "+r"(d), "+r"(e), "+r"(f), "+r"(g), "+r"(h));
    }
    sink = a ^ b ^ c ^ d ^ e ^ f ^ g ^ h;
}
static uint64_t rng = 88172645463325252ull;           /* own random numbers: the C library's  */
static uint64_t next_rand(void) {                     /* rand() is only 15-bit on some systems */
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng;
}
static void make_chain(uint32_t *p, size_t bytes) {   /* one random cycle through 64-byte lines */
    size_t n = bytes / 64;
    for (size_t i = 0; i < n; i++) p[i * 16] = (uint32_t)i;
    for (size_t i = n - 1; i > 0; i--) { size_t j = (size_t)(next_rand() % i); uint32_t t = p[i * 16]; p[i * 16] = p[j * 16]; p[j * 16] = t; }
}
static void chase(const uint32_t *p, long n) {        /* each load waits for the previous one */
    uint32_t x = 0; for (long i = 0; i < n; i++) x = p[x * 16]; sink = x;
}

/* least squares: cycles per access = c0 + cost[NEAR]*near + cost[FAR]*far */
#define MAXP 32
static int np; static double py[MAXP], px[MAXP][NC];
static void fit_costs(void) {
    int idx[3], m = 0; idx[m++] = -1;
    if (have[NEAR]) idx[m++] = NEAR;
    if (have[FAR])  idx[m++] = FAR;
    if (m == 1 || !have[CYC]) return;
    double A[3][4] = {{0}};
    for (int s = 0; s < np; s++) {
        double x[3]; for (int i = 0; i < m; i++) x[i] = idx[i] < 0 ? 1 : px[s][idx[i]];
        for (int i = 0; i < m; i++) { for (int j = 0; j < m; j++) A[i][j] += x[i] * x[j]; A[i][m] += x[i] * py[s]; }
    }
    for (int i = 0; i < m; i++)                                       /* Gaussian elimination */
        for (int r = 0; r < m; r++) if (r != i && A[i][i] != 0) {
            double f = A[r][i] / A[i][i];
            for (int j = i; j <= m; j++) A[r][j] -= f * A[i][j];
        }
    for (int i = 1; i < m; i++) cost[idx[i]] = (A[i][i] > 0 && A[i][m] / A[i][i] > 0) ? A[i][m] / A[i][i] : 0;
}

/* ---- 2. TIERS: memory speed by kind and size (works with no counters) ---- */
static const char *pt_reg[MAXP]; static size_t pt_size[MAXP]; static double pt_ns[MAXP];
static int big_reg = -1; static size_t big_size;

int pmu_init(void) {
    counters_open();
    snap_t a, b; delta_t d;
    busy_loop(PMU_STEPS * 10);                                    /* let the clock speed settle */
    take(&a); busy_loop(PMU_STEPS * 10); take(&b); d = sub(&a, &b);
    peak_ipc = have[INS] ? d.c[INS] / d.run : 0;
    if (have[INS] && d.c[INS] < PMU_STEPS) have[INS] = 0;        /* counter present but not counting */

    for (int r = 0; r < NREG; r++) {
        size_t max = regions[r].max; uint32_t *buf = NULL;
        while (max >= 4096 && !(buf = region_alloc(&regions[r], max))) max /= 2;   /* largest that fits */
        if (!buf) continue;
        for (size_t s = 4096; s <= max && np < MAXP; s *= 4) {
            make_chain(buf, s);
            chase(buf, PMU_STEPS / 4);                            /* warm up */
            take(&a); chase(buf, PMU_STEPS); take(&b); d = sub(&a, &b);
            pt_reg[np] = regions[r].name; pt_size[np] = s; pt_ns[np] = d.wall / PMU_STEPS;
            py[np] = d.run / PMU_STEPS;
            for (int k = 0; k < NC; k++) px[np][k] = d.c[k] / PMU_STEPS;
            np++;
            big_reg = r; big_size = s;
        }
        region_free(&regions[r], buf, max);
    }
    for (int k = NEAR; k <= FAR; k++) {                           /* a miss counter that never moved is useless */
        double mx = 0; for (int s = 0; s < np; s++) if (px[s][k] > mx) mx = px[s][k];
        if (have[k] && mx < 0.05) have[k] = 0;
    }
    fit_costs();

    const char *unit = have[CYC] ? "cycle" : "ns";
    printf("counters:%s%s%s%s%s\n", have[CYC] ? " cycles" : "", have[INS] ? " instructions" : "",
           have[NEAR] ? " first-level-misses" : "", have[FAR] ? " slowest-memory-misses" : "",
           (have[CYC] | have[INS] | have[NEAR] | have[FAR]) ? "" : " none (only waiting vs running can be shown)");
    printf("prices:   peak %.2f instructions/%s", peak_ipc, unit);
    if (have[NEAR]) printf(" | first-level miss +%.1f %ss", cost[NEAR], unit);
    if (have[FAR])  printf(" | slowest-memory miss +%.1f %ss", cost[FAR], unit);
    printf("\nmemory:  ");
    for (int s = 0, t = -1; s < np; s++) {
        if (s == 0 || pt_reg[s] != pt_reg[s - 1] || pt_ns[s] > 1.5 * pt_ns[s - 1]) printf(" | T%d", ++t);
        printf(" %s %lluKB=%.1fns", pt_reg[s], (unsigned long long)(pt_size[s] >> 10), pt_ns[s]);
    }
    printf("\n");
    return np ? 0 : -1;
}

void *pmu_big_buffer(size_t *bytes) {
    *bytes = big_size;
    return big_reg >= 0 ? region_alloc(&regions[big_reg], big_size) : NULL;
}

/* ---- 3. MEASURE ---- */
#define MAXS 16
static snap_t start[MAXS];
static delta_t tot[MAXS];
static long runs[MAXS];
void pmu_begin(int s) { take(&start[s]); }
void pmu_end(int s) {
    snap_t e; take(&e); delta_t d = sub(&start[s], &e);
    for (int k = 0; k < NC; k++) tot[s].c[k] += d.c[k];
    tot[s].cpu += d.cpu; tot[s].wall += d.wall; tot[s].run += d.run; runs[s]++;
}

/* ---- 4. EXPLAIN ---- */
void pmu_report(const char *names[], int n) {
    printf("\n%-9s %9s | %8s %8s %8s %8s %8s | %9s | %s\n", "stage", "time us",
           "compute", "mem:near", "mem:far", have[INS] ? "other" : "running", "waiting", "explained", "label");
    for (int s = 0; s < n; s++) {
        if (!runs[s]) continue;
        delta_t *d = &tot[s];
        double run = d->run > 0 ? d->run : 1;
        double compute = have[INS] ? d->c[INS] / peak_ipc : 0;        /* time if it ran at peak       */
        if (compute > run) compute = run;
        double lost = run - compute;                                   /* time the core was held up    */
        double mem_near = d->c[NEAR] * cost[NEAR], mem_far = d->c[FAR] * cost[FAR];
        double explained = (compute + mem_near + mem_far) / run;               /* accuracy check, before cap   */
        if (mem_near + mem_far > lost) { double f = lost / (mem_near + mem_far); mem_near *= f; mem_far *= f; }
        double other = lost - mem_near - mem_far;
        double r = have_cpu_clock ? d->cpu / d->wall : 1;              /* share of wall time running   */
        if (r > 1) r = 1;                                              /* clocks read a few ns apart   */
        double sh_cmp = r * compute / run, sh_near = r * mem_near / run, sh_far = r * mem_far / run;
        double sh_oth = r * other / run, sh_wait = 1 - r;
        /* label = the biggest share; MEMORY says which part of memory */
        const char *label = have[INS] ? "STALLED" : "RUNNING";
        double top = sh_oth;
        if (sh_cmp > top)          { top = sh_cmp;          label = "COMPUTE"; }
        if (sh_near + sh_far > top){ top = sh_near + sh_far; label = sh_far >= sh_near ? "MEMORY (slowest memory)" : "MEMORY (near memory)"; }
        if (sh_wait > top)         { top = sh_wait;         label = "WAIT"; }
        printf("%-9s %9.1f | %7.0f%% %7.0f%% %7.0f%% %7.0f%% %7.0f%% | ", names[s], d->wall / runs[s] / 1e3,
               100 * sh_cmp, 100 * sh_near, 100 * sh_far, 100 * sh_oth, 100 * sh_wait);
        if (have[INS]) printf("%8.0f%% | %s\n", 100 * explained, label); else printf("%9s | %s\n", "-", label);
    }
}
