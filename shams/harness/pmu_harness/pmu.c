/* pmu.c - see pmu.h.
 *
 * PART A (platform layer) is the only part that differs per board. It answers:
 *   - which counters exist, and how to read them (per thread where the board allows)
 *   - the clocks
 *   - which threads/tasks used the core, and for how long
 *   - which kinds of memory the board has, and how to allocate in each
 * PART B (the method) is identical on every board:
 *   1. CALIBRATE  measure this board's prices: instructions per cycle, cost of a miss
 *                 at each cache level
 *   2. TIERS      measure how slow each kind/size of memory is (timing only)
 *   3. MEASURE    read the counters at the start and end of each stage, per thread
 *   4. EXPLAIN    counted work x prices -> compute / memory per level / other / waiting;
 *                 where the core counts its memory-stall cycles, those set the memory
 *                 time, so misses that overlap (out-of-order cores) are not double-counted;
 *                 plus delays (switched out, memory total / exposed / hidden) and who
 *                 used the core during each stage                                          */
#define _GNU_SOURCE
#include "pmu.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Generic counters. Each is optional; have[k] says if this board has it.
 *   CYC  cycles                 INS  instructions finished
 *   M1   level-1 cache misses   (data came from level 2 or further)
 *   M2   level-2 cache misses   (data came from level 3 or further)
 *   M3   level-3 cache misses   (data came from main memory)
 *   STL  cycles the core could not proceed because it was waiting for memory.
 *        Measured, so it already accounts for misses that overlapped.
 * What the code did (instruction mix), where the board counts it:
 *   LD loads  ST stores  IOP integer ops  FOP floating-point ops  VOP SIMD ops
 *   BR branches  BMP mispredicted branches                                      */
enum { CYC, INS, M1, M2, M3, STL, LD, ST, IOP, FOP, VOP, BR, BMP, NC };
static const char *cname[NC] = { "cycles", "instructions", "L1-misses", "L2-misses", "L3-misses",
    "memory-stall-cycles", "loads", "stores", "int-ops", "fp-ops", "simd-ops", "branches", "mispredicts" };
#define NLV  3                                 /* miss levels M1..M3                   */
#define MAXS 16                                /* stages per thread                    */
#define MAXT 8                                 /* threads that mark stages             */
#define MAXU 24                                /* threads/tasks seen on the core       */
static int have[NC];
static int have_cpu_clock;                     /* can we tell running from waiting?    */
typedef struct { const char *name; int caps; size_t max; } region_t;

/* one photo of an instant, and the difference of two photos */
typedef struct { uint64_t c[NC], cpu, wall; } snap_t;
typedef struct { double c[NC], cpu, wall, run; } delta_t;
/* who used the core: running time so far of each thread/task, and the core's idle time */
typedef struct { int n, cpu; char name[MAXU][16]; uint64_t key[MAXU], ns[MAXU], wall; int64_t idle; } core_t;
/* everything kept for one thread of the measured program */
typedef struct {
    char name[16]; int active, watch;          /* watch: also record who used the core */
    int fd[NC]; uint32_t cpu_last; uint64_t cpu_total;
    snap_t start[MAXS]; delta_t tot[MAXS]; long runs[MAXS];
    core_t cstart; char uname[MAXS][MAXU][16]; double uns[MAXS][MAXU], uwall[MAXS], uidle[MAXS]; int un[MAXS];
} thr_t;
static thr_t threads[MAXT];
static int nthreads;
#if defined(ESP_PLATFORM) || defined(__linux__)
static __thread thr_t *me;                     /* the calling thread's slot */
#else
static thr_t *me;                              /* one thread only (GAP9 main core) */
#endif

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
#define CORE_UNIT_NS 1000                                   /* task run times are in us           */
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
#if defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS) && defined(CONFIG_FREERTOS_USE_TRACE_FACILITY) && \
    defined(CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER)
#define PMU_ESP_TASK_CLOCK 1               /* FreeRTOS counts how long each task has run (in us) */
static uint64_t cpu_ns(thr_t *t) {
    TaskStatus_t st;
    taskYIELD();                           /* close the current time slice: counter is now up to date */
    vTaskGetInfo(NULL, &st, pdFALSE, eRunning);
    uint32_t now = (uint32_t)st.ulRunTimeCounter;
    t->cpu_total += (uint32_t)(now - t->cpu_last); t->cpu_last = now;   /* survives 32-bit wrap */
    return t->cpu_total * 1000;
}
static void core_read(core_t *c) {         /* every task's running time, from the same statistics */
    static TaskStatus_t st[MAXU];
    taskYIELD();
    int n = (int)uxTaskGetSystemState(st, MAXU, NULL), me_core = esp_cpu_get_core_id();
    c->n = 0; c->idle = -1; c->cpu = me_core; c->wall = now_ns();
    for (int i = 0; i < n; i++) {
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
        if (st[i].xCoreID != me_core && st[i].xCoreID != tskNO_AFFINITY) continue;   /* other core */
#endif
        snprintf(c->name[c->n], 16, "%s", strncmp(st[i].pcTaskName, "IDLE", 4) ? st[i].pcTaskName : "idle");
        c->key[c->n] = (uintptr_t)st[i].xHandle;
        c->ns[c->n] = (uint32_t)st[i].ulRunTimeCounter;           /* us, 32-bit */
        c->n++;
    }
}
#else
static uint64_t cpu_ns(thr_t *t) { (void)t; return now_ns(); }  /* no per-task clock: waiting shows as STALLED */
static void core_read(core_t *c) { c->n = 0; c->idle = -1; c->cpu = 0; c->wall = now_ns(); }
#endif
void pmu_sleep_us(unsigned us) {                          /* at least one RTOS tick (set FREERTOS_HZ=1000) */
    TickType_t t = pdMS_TO_TICKS((us + 999) / 1000); vTaskDelay(t ? t : 1); }
/* spawn: tasks on this core at this task's priority, so FreeRTOS time-slices them */
static volatile int alive; static void (*spawn_fn)(int);
static void spawn_task(void *a) {
    pmu_thread(pcTaskGetName(NULL));                        /* measured under its task name */
    spawn_fn((int)(intptr_t)a); pmu_thread_done(); alive--; vTaskDelete(NULL);
}
int pmu_spawn(int n, const char *name, void (*fn)(int id)) {
    spawn_fn = fn;
    for (int i = 0; i < n; i++) {
        char nm[16]; snprintf(nm, sizeof nm, "%s%d", name, i + 1);
        if (xTaskCreatePinnedToCore(spawn_task, nm, 4096, (void *)(intptr_t)i, uxTaskPriorityGet(NULL),
                                    NULL, esp_cpu_get_core_id()) == pdPASS) alive++;
    }
    return alive;
}
void pmu_join(void) { while (alive > 0) vTaskDelay(1); }
static void set_thread_name(const char *name) { (void)name; }   /* FreeRTOS names are set at creation */
static void counters_open(thr_t *t) {
    (void)t;
    have[CYC] = 1;                                          /* every ESP32 counts cycles */
#ifdef PMU_ESP_TASK_CLOCK
    have_cpu_clock = 1;
#endif
#if defined(__riscv)
    have[INS] = 1;                                          /* RISC-V instret (Zicntr); see note in README */
#endif
#ifdef PMU_ESP_CACHE_CNT
    if (esp_cache_cnt_num_units() > 0 && esp_cache_cnt_start() == ESP_OK) have[M1] = have[M2] = 1;
#endif
}
static void thread_open(thr_t *t)  { (void)t; }             /* counters are per core, not per task */
static void thread_close(thr_t *t) { (void)t; }
static void counters_read(thr_t *t, uint64_t v[NC]) {
    (void)t;
    memset(v, 0, NC * sizeof v[0]);
    v[CYC] = esp_cpu_get_cycle_count();
#if defined(__riscv)
    uint32_t ins; __asm__ volatile("rdinstret %0" : "=r"(ins)); v[INS] = ins;
#endif
#ifdef PMU_ESP_CACHE_CNT
    int core = esp_cpu_get_core_id();
    for (size_t u = 0; u < esp_cache_cnt_num_units(); u++) {   /* line fills (true misses) per level:   */
                                                               /* L1 = first cache, L2 = PSRAM/flash cache */
        esp_cache_cnt_unit_info_t info; esp_cache_cnt_data_t d;
        if (esp_cache_cnt_get_unit_info(u, &info) != ESP_OK || esp_cache_cnt_get(u, &d) != ESP_OK) continue;
        if (!(d.valid_mask & ESP_CACHE_CNT_VALID_LINE_FILLS)) continue;
        if (info.core_id != core && info.core_id != -1) continue;
        v[info.cache_level <= 1 ? M1 : M2] += d.line_fills;
    }
#endif
}

#elif defined(__has_include) && __has_include("pmsis.h")      /* ---- GAP9 (PMSIS) ---- */
#include "pmsis.h"
#define PMU_STEPS 200000
#define WRAP32 1
#define CORE_UNIT_NS 1
static region_t regions[] = { { "L2", 0, 512 << 10 } };     /* main core (FC) sees L2 directly */
static void *region_alloc(const region_t *r, size_t n) { (void)r; return pi_l2_malloc(n); }
static void  region_free(const region_t *r, void *p, size_t n) { (void)r; pi_l2_free(p, n); }
static uint64_t now_ns(void) { return (uint64_t)pi_time_get_us() * 1000; }
static uint64_t cpu_ns(thr_t *t) { (void)t; return now_ns(); }
static void core_read(core_t *c) { c->n = 0; c->idle = -1; c->cpu = 0; c->wall = now_ns(); }
void pmu_sleep_us(unsigned us) { pi_time_wait_us(us); }
int  pmu_spawn(int n, const char *name, void (*fn)(int id)) { (void)n; (void)name; (void)fn; return 0; }  /* no scheduler */
void pmu_join(void) { }
static void set_thread_name(const char *name) { (void)name; }
static void counters_open(thr_t *t) {                       /* real chip: cycles + ONE other event */
    (void)t;
    pi_perf_conf((1 << PI_PERF_CYCLES) | (1 << PI_PERF_INSTR));
    pi_perf_reset(); pi_perf_start();
    have[CYC] = have[INS] = 1;
}
static void thread_open(thr_t *t)  { (void)t; }
static void thread_close(thr_t *t) { (void)t; }
static void counters_read(thr_t *t, uint64_t v[NC]) {
    (void)t;
    memset(v, 0, NC * sizeof v[0]);
    v[CYC] = pi_perf_read(PI_PERF_CYCLES);
    v[INS] = pi_perf_read(PI_PERF_INSTR);
}

#elif defined(__linux__)                                    /* ---- Jetson, Raspberry Pi, Linux PC ---- */
#include <linux/perf_event.h>
#include <dirent.h>
#include <glob.h>
#include <pthread.h>
#include <sched.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#define PMU_STEPS 2000000
#define WRAP32 0
#define CORE_UNIT_NS 1
static region_t regions[] = { { "RAM", 0, 64 << 20 } };     /* caches are found by size, not by place */
static void *region_alloc(const region_t *r, size_t n) { (void)r; return aligned_alloc(64, n); }
static void  region_free(const region_t *r, void *p, size_t n) { (void)r; (void)n; free(p); }
static uint64_t clk(clockid_t id) { struct timespec t; clock_gettime(id, &t); return t.tv_sec * 1000000000ull + t.tv_nsec; }
static uint64_t now_ns(void) { return clk(CLOCK_MONOTONIC_RAW); }
static uint64_t cpu_ns(thr_t *t) { (void)t; return clk(CLOCK_THREAD_CPUTIME_ID); }   /* only while this thread runs */
void pmu_sleep_us(unsigned us) { usleep(us); }
static uint64_t read_u64(const char *path) {                /* first number in a small /proc or /sys file */
    FILE *f = fopen(path, "r"); unsigned long long v = 0;
    if (f) { if (fscanf(f, "%llu", &v) != 1) v = 0; fclose(f); }
    return v;
}
/* who used the core: running time of every thread of this program (/proc/self/task/<tid>/schedstat,
 * first number = ns run), and this core's idle time (cpuidle, us; up to date because we are running) */
static void core_read(core_t *c) {
    char p[300]; pid_t self = (pid_t)syscall(SYS_gettid);
    c->n = 0; c->cpu = sched_getcpu(); c->wall = now_ns();
    DIR *d = opendir("/proc/self/task"); struct dirent *e;
    while (d && (e = readdir(d)) && c->n < MAXU) {
        if (e->d_name[0] == '.') continue;
        pid_t tid = (pid_t)atoi(e->d_name);
        snprintf(p, sizeof p, "/proc/self/task/%s/comm", e->d_name);
        FILE *f = fopen(p, "r"); if (!f) continue;
        if (!fgets(c->name[c->n], 16, f)) c->name[c->n][0] = 0;
        fclose(f); c->name[c->n][strcspn(c->name[c->n], "\n")] = 0;
        snprintf(p, sizeof p, "/proc/self/task/%s/schedstat", e->d_name);
        c->ns[c->n] = tid == self ? clk(CLOCK_THREAD_CPUTIME_ID) : read_u64(p);   /* own thread: exact */
        if (tid == self && me) snprintf(c->name[c->n], 16, "%s", me->name);
        c->key[c->n++] = (uint64_t)tid;
    }
    if (d) closedir(d);
    glob_t g; c->idle = -1;
    snprintf(p, sizeof p, "/sys/devices/system/cpu/cpu%d/cpuidle/state*/time", c->cpu);
    if (glob(p, 0, NULL, &g) == 0) {
        c->idle = 0;
        for (size_t i = 0; i < g.gl_pathc; i++) c->idle += (int64_t)read_u64(g.gl_pathv[i]) * 1000;
        globfree(&g);
    }
}
/* spawn: threads pinned to this thread's core, so the scheduler must switch between them */
static pthread_t sp_th[MAXT]; static int sp_n; static void (*spawn_fn)(int); static const char *sp_name;
static void *spawn_thread(void *a) {
    char nm[16]; snprintf(nm, sizeof nm, "%s%d", sp_name, (int)(intptr_t)a + 1);
    pmu_thread(nm);                                         /* measured under its own name */
    spawn_fn((int)(intptr_t)a); pmu_thread_done(); return NULL;
}
int pmu_spawn(int n, const char *name, void (*fn)(int id)) {
    cpu_set_t one; CPU_ZERO(&one); CPU_SET(sched_getcpu(), &one);
    sched_setaffinity(0, sizeof one, &one);                 /* stay on this core ...            */
    pthread_attr_t at; pthread_attr_init(&at);
    pthread_attr_setaffinity_np(&at, sizeof one, &one);     /* ... and put the new threads on it */
    spawn_fn = fn; sp_name = name;
    for (sp_n = 0; sp_n < n && sp_n < MAXT; sp_n++)
        if (pthread_create(&sp_th[sp_n], &at, spawn_thread, (void *)(intptr_t)sp_n) != 0) break;
    pthread_attr_destroy(&at);
    return sp_n;
}
void pmu_join(void) { for (int i = 0; i < sp_n; i++) pthread_join(sp_th[i], NULL); sp_n = 0; }
static void set_thread_name(const char *name) { pthread_setname_np(pthread_self(), name); }
static struct { uint32_t type; uint64_t cfg; } ev[NC];     /* the events chosen at start-up */
/* one counter for one thread (tid 0 = the calling thread). If there are more counters than
 * the core has, Linux takes turns; enabled/running times let us scale the counts back up. */
static int perf_open_tid(uint32_t type, uint64_t config, pid_t tid, int kernel_too) {
    struct perf_event_attr a;
    memset(&a, 0, sizeof a);
    a.size = sizeof a; a.type = type; a.config = config;
    a.read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
    a.exclude_kernel = !kernel_too; a.exclude_hv = 1;              /* normally: only the program's own code */
    return (int)syscall(SYS_perf_event_open, &a, tid, -1, -1, 0);
}
static int perf_open(uint32_t type, uint64_t config) { return perf_open_tid(type, config, 0, 0); }
static uint64_t perf_value(int f) {                               /* count, scaled for turn-taking */
    uint64_t r[3];
    if (f < 0 || read(f, r, sizeof r) != sizeof r || r[2] == 0) return 0;
    return r[2] < r[1] ? (uint64_t)((double)r[0] * r[1] / r[2]) : r[0];
}
#define READ_MISS(c) ((c) | (PERF_COUNT_HW_CACHE_OP_READ << 8) | (PERF_COUNT_HW_CACHE_RESULT_MISS << 16))
/* ARM: the kernel lists, under events/, only the events this core really implements */
static int arm_has(const char *name) {
    char pat[160]; glob_t g; int ok;
    if (glob("/sys/bus/event_source/devices/armv8*/events", 0, NULL, &g) != 0) return 1;  /* no list: just try */
    globfree(&g);
    snprintf(pat, sizeof pat, "/sys/bus/event_source/devices/armv8*/events/%s", name);
    ok = glob(pat, 0, NULL, &g) == 0; globfree(&g);
    return ok;
}
static void counters_open(thr_t *t) {
    struct { int k; uint32_t type; uint64_t cfg; const char *arm; } tries[] = {
#if defined(__aarch64__)   /* Jetson Orin Nano (Cortex-A78AE), Raspberry Pi 5 (Cortex-A76) */
        { CYC, PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES,   NULL },
        { INS, PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS, NULL },
        { M1,  PERF_TYPE_RAW, 0x03,   "l1d_cache_refill"  },   /* L1D_CACHE_REFILL                   */
        { M2,  PERF_TYPE_RAW, 0x17,   "l2d_cache_refill"  },   /* L2D_CACHE_REFILL                   */
        { M3,  PERF_TYPE_RAW, 0x2A,   "l3d_cache_refill"  },   /* L3D_CACHE_REFILL                   */
        { STL, PERF_TYPE_RAW, 0x4005, "stall_backend_mem" },   /* memory-stall cycles (A78AE)        */
        { STL, PERF_TYPE_RAW, 0x24,   "stall_backend"     },   /* all back-end stalls (A76): checked */
        { LD,  PERF_TYPE_RAW, 0x70,   "ld_spec"           },   /* loads                              */
        { ST,  PERF_TYPE_RAW, 0x71,   "st_spec"           },   /* stores                             */
        { IOP, PERF_TYPE_RAW, 0x73,   "dp_spec"           },   /* integer data-processing ops        */
        { FOP, PERF_TYPE_RAW, 0x75,   "vfp_spec"          },   /* floating-point ops                 */
        { VOP, PERF_TYPE_RAW, 0x74,   "ase_spec"          },   /* SIMD (NEON) ops                    */
        { BR,  PERF_TYPE_RAW, 0x21,   "br_retired"        },   /* branches                           */
        { BMP, PERF_TYPE_RAW, 0x22,   "br_mis_pred_retired" }, /* mispredicted branches              */
        { BMP, PERF_TYPE_RAW, 0x10,   "br_mis_pred"       },
#else                      /* other Linux machines: generic events only                          */
        { CYC, PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES,   NULL },
        { INS, PERF_TYPE_HARDWARE, PERF_COUNT_HW_INSTRUCTIONS, NULL },
        { M1,  PERF_TYPE_HW_CACHE, READ_MISS(PERF_COUNT_HW_CACHE_L1D), NULL },
        { M3,  PERF_TYPE_HW_CACHE, READ_MISS(PERF_COUNT_HW_CACHE_LL),  NULL },
        { STL, PERF_TYPE_HARDWARE, PERF_COUNT_HW_STALLED_CYCLES_BACKEND, NULL },
        { LD,  PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_READ << 8), NULL },
        { ST,  PERF_TYPE_HW_CACHE, PERF_COUNT_HW_CACHE_L1D | (PERF_COUNT_HW_CACHE_OP_WRITE << 8), NULL },
        { BR,  PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_INSTRUCTIONS, NULL },
        { BMP, PERF_TYPE_HARDWARE, PERF_COUNT_HW_BRANCH_MISSES, NULL },
#endif
    };
    for (size_t i = 0; i < sizeof tries / sizeof tries[0]; i++) {
        int k = tries[i].k;
        if (have[k]) continue;
        if (tries[i].arm && !arm_has(tries[i].arm)) continue;    /* not implemented on this core */
        int f = perf_open(tries[i].type, tries[i].cfg);
        if (f >= 0) { t->fd[k] = f; have[k] = 1; ev[k].type = tries[i].type; ev[k].cfg = tries[i].cfg; }
    }
    have_cpu_clock = 1;
}
static void thread_open(thr_t *t) {                         /* a new thread opens the same events */
    for (int k = 0; k < NC; k++) t->fd[k] = have[k] ? perf_open(ev[k].type, ev[k].cfg) : -1;
}
static void thread_close(thr_t *t) { for (int k = 0; k < NC; k++) if (t->fd[k] >= 0) close(t->fd[k]); }
static void counters_read(thr_t *t, uint64_t v[NC]) {
    for (int k = 0; k < NC; k++) v[k] = have[k] ? perf_value(t->fd[k]) : 0;
}

#else
#error "pmu.c: unknown platform (supported: Linux, ESP-IDF, GAP9/PMSIS)"
#endif

#define NREG (int)(sizeof regions / sizeof regions[0])

/* ============================== PART B: the method ============================== */

static void take(thr_t *t, snap_t *s) { counters_read(t, s->c); s->cpu = cpu_ns(t); s->wall = now_ns(); }
static delta_t sub(const snap_t *a, const snap_t *b) {
    delta_t d;
    for (int k = 0; k < NC; k++) d.c[k] = WRAP32 ? (double)(uint32_t)(b->c[k] - a->c[k]) : (double)(b->c[k] - a->c[k]);
    d.cpu = (double)(b->cpu - a->cpu); d.wall = (double)(b->wall - a->wall);
    d.run = have[CYC] ? d.c[CYC] : d.cpu;          /* running time: cycles if counted, else ns */
    return d;
}

/* ---- threads: each thread that marks stages gets its own slot ---- */
static volatile int slot_lock;
void pmu_thread(const char *name) {
    while (__atomic_test_and_set(&slot_lock, __ATOMIC_ACQUIRE)) { }
    thr_t *t = NULL;
    for (int i = 0; i < nthreads && !t; i++)                      /* same name again: keep its totals */
        if (!threads[i].active && name && !strcmp(threads[i].name, name)) t = &threads[i];
    if (!t && nthreads < MAXT) { t = &threads[nthreads++]; snprintf(t->name, 16, "%s", name ? name : "thread"); }
    if (t) t->active = 1;
    __atomic_clear(&slot_lock, __ATOMIC_RELEASE);
    if (!t) return;                                               /* too many threads: not measured */
    set_thread_name(t->name);
    thread_open(t);
    me = t;
}
void pmu_thread_done(void) { if (me) { thread_close(me); me->active = 0; me = NULL; } }

/* ---- 1. CALIBRATE ---- */
static double peak_ipc;      /* most instructions per cycle this core can finish     */
static double cost[NC];      /* extra cycles one miss at level 1 / 2 / 3 costs        */
static double ns_per_cycle;  /* length of one cycle at full speed                    */
static double fp_wait;       /* extra cycles a floating-point op waits for the previous result */
static volatile uint64_t sink;

static void busy_loop(long n) {     /* 8 independent adds: no memory, nothing to wait for */
    uint32_t a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0, h = 0;
    for (long i = 0; i < n; i++) {
        a += i; b += i; c += i; d += i; e += i; f += i; g += i; h += i;
        __asm__ volatile("" : "+r"(a), "+r"(b), "+r"(c), "+r"(d), "+r"(e), "+r"(f), "+r"(g), "+r"(h));
    }
    sink = a ^ b ^ c ^ d ^ e ^ f ^ g ^ h;
}
#if defined(__aarch64__)                /* keep the value in an FP register, untouched */
#  define FP_REG "+w"
#elif defined(__x86_64__)
#  define FP_REG "+x"
#elif defined(__riscv)
#  define FP_REG "+f"
#endif
static void fp_chain(long n) {      /* one chain: each op needs the previous result */
    float a = 1;
#ifdef FP_REG
    for (long i = 0; i < n; i++) { a = a * 0.9999f + 1e-4f; __asm__ volatile("" : FP_REG(a)); }
#else
    for (long i = 0; i < n; i++) a = a * 0.9999f + 1e-4f;
#endif
    sink = (uint64_t)a;
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

/* least squares: cycles per access = c0 + cost[M1]*m1 + cost[M2]*m2 + cost[M3]*m3
 * (a miss that reaches main memory is counted at every level, so its costs add up) */
#define MAXP 32
static int np; static double py[MAXP], px[MAXP][NC];
static void fit_costs(void) {
    int idx[NLV + 1], m = 0; idx[m++] = -1;                           /* -1 = the constant c0 */
    for (int k = M1; k <= M3; k++) if (have[k]) idx[m++] = k;
    if (m == 1 || !have[CYC]) return;
    double A[NLV + 1][NLV + 2] = {{0}};
    for (int s = 0; s < np; s++) {
        double x[NLV + 1]; for (int i = 0; i < m; i++) x[i] = idx[i] < 0 ? 1 : px[s][idx[i]];
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
    me = &threads[0]; nthreads = 1;                               /* the calling thread: "main" */
    snprintf(me->name, 16, "main"); me->active = 1; me->watch = 1;
    counters_open(me);
    snap_t a, b; delta_t d;
    busy_loop(PMU_STEPS * 10);                                    /* let the clock speed settle */
    take(me, &a); busy_loop(PMU_STEPS * 10); take(me, &b); d = sub(&a, &b);
    peak_ipc = have[INS] ? d.c[INS] / d.run : 0;
    ns_per_cycle = have[CYC] && d.c[CYC] > 0 ? d.cpu / d.c[CYC] : 0;  /* clock speed, from the same loop */
    if (have[INS] && d.c[INS] < PMU_STEPS) have[INS] = 0;        /* counter present but not counting */
    double stall_busy = have[STL] ? d.c[STL] / d.run : 0;         /* memory stalls with no memory use */
    if (have[FOP] && have[CYC]) {                                 /* cost of waiting for an FP result */
        take(me, &a); fp_chain(PMU_STEPS); take(me, &b); d = sub(&a, &b);
        if (d.c[FOP] > PMU_STEPS / 2) {
            fp_wait = d.c[CYC] / d.c[FOP] - (peak_ipc > 0 ? 1 / peak_ipc : 0);
            if (fp_wait < 0) fp_wait = 0;
        } else have[FOP] = 0;                                     /* counter present but not counting */
    }

    for (int r = 0; r < NREG; r++) {
        size_t max = regions[r].max; uint32_t *buf = NULL;
        while (max >= 4096 && !(buf = region_alloc(&regions[r], max))) max /= 2;   /* largest that fits */
        if (!buf) continue;
        for (size_t s = 4096; s <= max && np < MAXP; s *= 4) {
            make_chain(buf, s);
            chase(buf, PMU_STEPS / 4);                            /* warm up */
            take(me, &a); chase(buf, PMU_STEPS); take(me, &b); d = sub(&a, &b);
            pt_reg[np] = regions[r].name; pt_size[np] = s; pt_ns[np] = d.wall / PMU_STEPS;
            py[np] = d.run / PMU_STEPS;
            for (int k = 0; k < NC; k++) px[np][k] = d.c[k] / PMU_STEPS;
            np++;
            big_reg = r; big_size = s;
        }
        region_free(&regions[r], buf, max);
    }
    for (int k = M1; k <= M3; k++) {                              /* a miss counter that never moved is useless */
        double mx = 0; for (int s = 0; s < np; s++) if (px[s][k] > mx) mx = px[s][k];
        if (have[k] && mx < 0.05) have[k] = 0;
    }
    /* a stall counter must be ~0 on the busy loop and high on the largest chase */
    if (have[STL] && (stall_busy > 0.2 || !np || px[np - 1][STL] < 0.5 * py[np - 1])) have[STL] = 0;
    fit_costs();

    const char *unit = have[CYC] ? "cycle" : "ns";
    printf("counters:");
    for (int k = 0; k < NC; k++) if (have[k]) printf(" %s", cname[k]);
    printf("%s\n", (have[CYC] | have[INS]) ? "" : " none (only waiting vs running can be shown)");
    printf("prices:   peak %.2f instructions/%s", peak_ipc, unit);
    for (int k = M1; k <= M3; k++) if (have[k]) printf(" | L%d miss +%.1f %ss", k - M1 + 1, cost[k], unit);
    if (fp_wait > 0) printf(" | dependent FP op +%.1f %ss", fp_wait, unit);
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

/* ---- 3. MEASURE ----
 * begin: (who used the core) then counter photo; end: counter photo then (who used the
 * core), so reading the thread list is never inside the measured stage.               */
void pmu_begin(int s) {
    if (!me) pmu_thread(NULL);                                    /* unregistered thread */
    if (!me) return;
    if (me->watch) core_read(&me->cstart);
    take(me, &me->start[s]);
}
void pmu_end(int s) {
    if (!me) return;
    thr_t *t = me; snap_t e; take(t, &e); delta_t d = sub(&t->start[s], &e);
    for (int k = 0; k < NC; k++) t->tot[s].c[k] += d.c[k];
    t->tot[s].cpu += d.cpu; t->tot[s].wall += d.wall; t->tot[s].run += d.run; t->runs[s]++;
    if (!t->watch) return;
    static core_t c; core_read(&c); core_t *a = &t->cstart;
    t->uwall[s] += (double)(c.wall - a->wall);
    if (c.idle >= 0 && a->idle >= 0 && c.cpu == a->cpu) t->uidle[s] += (double)(c.idle - a->idle);
    else t->uidle[s] = -1e30;                                     /* idle time not known */
    for (int i = 0; i < c.n; i++) {                               /* each thread's running time in the stage */
        uint64_t was = 0;
        for (int j = 0; j < a->n; j++) if (a->key[j] == c.key[i]) was = a->ns[j];
        double ran = (WRAP32 ? (double)(uint32_t)(c.ns[i] - was) : (double)(c.ns[i] - was)) * CORE_UNIT_NS;
        int u = 0;                                                /* add it up by name */
        while (u < t->un[s] && strcmp(t->uname[s][u], c.name[i])) u++;
        if (u == t->un[s]) { if (u == MAXU) continue; snprintf(t->uname[s][u], 16, "%s", c.name[i]); t->un[s]++; }
        t->uns[s][u] += ran;
    }
}

/* ---- 4. EXPLAIN ----
 * Every row (a stage, or a thread) is explained the same way. Its time is split into five
 * CATEGORIES, each with SUB-CATEGORIES where the board can tell them apart:
 *   COMPUTE   issue (instructions at full speed), waiting on arithmetic results
 *   MEMORY    L1 / L2 / L3 misses (only the levels this board has and counts)
 *   STALLED   other pipeline stalls the counters do not explain
 *   KERNEL    the operating system working for this thread (system calls, page faults)
 *   WAITING   preempted (ready, another thread had the core), blocked (sleep, I/O)
 * Categories are the same on every board, so boards can be compared; sub-categories show
 * the detail each board offers.
 *
 * model  = misses x prices, per level (what the misses would cost one at a time)
 * memory = model, but never more than the measured memory-stall cycles (if counted)
 *          and never more than the cycles: overlapping misses cost less               */
typedef struct { double model, mem, lv[NLV], compute, arith, other, overlap, explained; } expl_t;
static expl_t explain(const double c[NC], double run) {            /* run = cycles */
    expl_t e; e.model = 0;
    if (run <= 0) run = 1;
    for (int i = 0; i < NLV; i++) { e.lv[i] = c[M1 + i] * cost[M1 + i]; e.model += e.lv[i]; }
    e.mem = e.model < run ? e.model : run;
    if (have[STL] && c[STL] < e.mem) e.mem = c[STL];                /* measured stalls win      */
    e.overlap = e.mem > 0 ? e.model / e.mem : 0;                    /* >1: misses overlapped    */
    for (int i = 0; i < NLV; i++) e.lv[i] = e.model > 0 ? e.lv[i] / e.model * e.mem : 0;  /* split by level */
    e.compute = have[INS] && peak_ipc > 0 ? c[INS] / peak_ipc : 0;  /* time if it ran at peak   */
    if (e.compute > run - e.mem) e.compute = run - e.mem;
    /* waiting for arithmetic results: FP/SIMD ops x the wait of a dependent op, at most what is left */
    e.arith = have[FOP] ? (c[FOP] + (have[VOP] ? c[VOP] : 0)) * fp_wait : 0;
    if (e.arith > run - e.mem - e.compute) e.arith = run - e.mem - e.compute;
    e.other = run - e.mem - e.compute - e.arith;                    /* what the counters miss   */
    e.explained = (e.compute + e.arith + e.mem) / run;
    return e;
}

/* one row to explain: counts, running time, time waiting to run (-1 = unknown), lifetime */
typedef struct { char name[16]; double c[NC], run, delay, wall; long ivcs, vcs; } row_t;
enum { C_COMPUTE, C_MEMORY, C_STALLED, C_KERNEL, C_WAITING, NCAT };
static const char *catname[NCAT] = { "COMPUTE", "MEMORY", "STALLED", "KERNEL", "WAITING" };
typedef struct { double cat[NCAT], issue, arith, lv[NLV], preempt, blocked; expl_t e; int counted; } split_t;

static split_t split_time(const row_t *w) {          /* fractions of the row's wall time */
    split_t r; memset(&r, 0, sizeof r);
    double wall = w->wall > 0 ? w->wall : 1, run = w->run;
    double user = have[CYC] && ns_per_cycle > 0 ? w->c[CYC] * ns_per_cycle : run;   /* in its own code */
    if (user > run) user = run;
    r.cat[C_KERNEL] = (run - user) / wall;
    double waiting = wall - run > 0 ? (wall - run) / wall : 0;
    r.cat[C_WAITING] = waiting;
    if (w->delay >= 0) {                                       /* split waiting where the kernel says */
        r.preempt = w->delay / wall < waiting ? w->delay / wall : waiting;
        r.blocked = waiting - r.preempt;
    } else r.preempt = r.blocked = -1;                         /* not split on this board */
    r.counted = have[CYC] && have[INS] && w->c[CYC] > 0;
    if (r.counted) {
        r.e = explain(w->c, w->c[CYC]);
        double k = user / wall / w->c[CYC];
        r.issue = r.e.compute * k; r.arith = r.e.arith * k;
        for (int i = 0; i < NLV; i++) r.lv[i] = r.e.lv[i] * k;
        r.cat[C_COMPUTE] = r.issue + r.arith;
        r.cat[C_MEMORY] = r.e.mem * k;
        r.cat[C_STALLED] = r.e.other * k;
    } else r.cat[C_STALLED] = user / wall;                     /* running, cause unknown */
    return r;
}
static void label_of(const split_t *r, char *out, size_t n) {    /* "CATEGORY / sub-category" */
    int b = 0; for (int i = 1; i < NCAT; i++) if (r->cat[i] > r->cat[b]) b = i;
    const char *sub = "";
    if (b == C_COMPUTE) sub = r->arith > r->issue ? " / waiting on results" : " / issue";
    if (b == C_MEMORY) {
        int l = 0; for (int i = 1; i < NLV; i++) if (r->lv[i] > r->lv[l]) l = i;
        static const char *lv[NLV] = { " / L1 misses", " / L2 misses", " / L3 misses" };
        sub = lv[l];
    }
    if (b == C_WAITING && r->preempt >= 0) sub = r->preempt > r->blocked ? " / preempted" : " / blocked";
    if (b == C_STALLED && !r->counted) { snprintf(out, n, "RUNNING"); return; }
    snprintf(out, n, "%s%s", catname[b], sub);
}
static void pct(double v, int ok) { if (ok && v >= 0) printf(" %6.1f%%", 100 * v); else printf(" %7s", "-"); }

/* the four tables, for any list of rows (stages of one thread, or threads of a program) */
static void print_tables(const char *what, row_t *rows, int n, int total_row) {
    char lab[48];
    printf("\n-- what the code did (- = not counted on this board) --\n");
    printf("%-12s %9s %5s | %7s %7s %7s %7s %7s %7s | %7s | %7s %7s %7s\n", what, "Minstr", "IPC",
           "loads", "stores", "int", "fp", "simd", "branch", "mispred", "L1/kI", "L2/kI", "L3/kI");
    for (int i = 0; i < n; i++) {
        row_t *t = &rows[i]; double I = t->c[INS] > 0 ? t->c[INS] : 1;
        if (total_row && i == n - 1) printf("----\n");
        printf("%-12s %9.2f %5.2f |", t->name, t->c[INS] / 1e6, have[CYC] && t->c[CYC] > 0 ? t->c[INS] / t->c[CYC] : 0);
        for (int k = LD; k <= BR; k++) pct(t->c[k] / I, have[k] && have[INS]);
        printf(" |"); pct(t->c[BR] > 0 ? t->c[BMP] / t->c[BR] : 0, have[BMP] && have[BR]);
        printf(" |");
        for (int k = M1; k <= M3; k++) if (have[k] && have[INS]) printf(" %7.1f", 1000 * t->c[k] / I); else printf(" %7s", "-");
        printf("\n");
    }
    printf("(ops and branches: %% of instructions; mispred: %% of branches; misses per 1000 instructions)\n");

    printf("\n-- where the time went: CATEGORIES (same on every board; share of wall time) --\n");
    printf("%-12s %10s | %8s %8s %8s %8s %8s | %s\n", what, "time ms", "COMPUTE", "MEMORY",
           have[CYC] && have[INS] ? "STALLED" : "RUNNING", "KERNEL", "WAITING", "label");
    for (int i = 0; i < n; i++) {
        split_t r = split_time(&rows[i]); label_of(&r, lab, sizeof lab);
        if (total_row && i == n - 1) printf("----\n");
        printf("%-12s %10.2f |", rows[i].name, rows[i].wall / 1e6);
        for (int c = 0; c < NCAT; c++) printf(" %7.1f%%", 100 * r.cat[c]);
        printf(" | %s\n", lab);
    }

    printf("\n-- sub-categories (the detail this board can give) --\n");
    printf("%-12s | %7s %7s | %7s %7s %7s | %7s %7s | %9s %7s\n", what, "issue", "arith", "L1miss", "L2miss", "L3miss",
           "preempt", "blocked", "explained", "overlap");
    for (int i = 0; i < n; i++) {
        split_t r = split_time(&rows[i]);
        if (total_row && i == n - 1) printf("----\n");
        printf("%-12s |", rows[i].name);
        pct(r.issue, r.counted); pct(r.arith, r.counted && have[FOP]); printf(" |");
        for (int k = 0; k < NLV; k++) pct(r.lv[k], r.counted && have[M1 + k]);
        printf(" |"); pct(r.preempt, 1); pct(r.blocked, 1);
        if (r.counted) printf(" | %8.0f%%", 100 * r.e.explained); else printf(" | %9s", "-");
        if (r.e.overlap > 0) printf(" %6.1fx\n", r.e.overlap); else printf(" %7s\n", "-");
    }
    printf("(explained: share of running cycles the counted work accounts for; overlap: misses in flight)\n");

    printf("\n-- delays (ms): memory total = every miss's full latency; exposed = the core stopped;\n"
           "   hidden = covered by other work (pipelining, misses in parallel) --\n");
    printf("%-12s | %10s %10s %10s %8s | %10s %10s | %9s %9s\n", what, "mem total", "exposed", "hidden", "hidden%",
           "preempted", "blocked", "forced-sw", "volunt-sw");
    for (int i = 0; i < n; i++) {
        row_t *t = &rows[i]; split_t r = split_time(t);
        double tau = ns_per_cycle / 1e6, hid = r.e.model > r.e.mem ? r.e.model - r.e.mem : 0;
        if (total_row && i == n - 1) printf("----\n");
        printf("%-12s | %10.3f %10.3f %10.3f %7.0f%% |", t->name, r.e.model * tau, r.e.mem * tau, hid * tau,
               r.e.model > 0 ? 100 * hid / r.e.model : 0);
        if (r.preempt >= 0) printf(" %10.3f %10.3f |", r.preempt * t->wall / 1e6, r.blocked * t->wall / 1e6);
        else printf(" %10s %10s |", "-", "-");
        if (t->ivcs >= 0) printf(" %9ld %9ld\n", t->ivcs, t->vcs); else printf(" %9s %9s\n", "-", "-");
    }
}

/* stage report: one table set per measured thread, values per run */
static void report_thread(thr_t *t, const char *names[], int n) {
    row_t rows[MAXS]; int m = 0;
    for (int s = 0; s < n; s++) {
        if (!t->runs[s]) continue;
        row_t *r = &rows[m++]; delta_t *d = &t->tot[s]; double k = 1.0 / t->runs[s];
        snprintf(r->name, 16, "%s", names[s]);
        for (int j = 0; j < NC; j++) r->c[j] = d->c[j] * k;
        r->run = (have_cpu_clock ? d->cpu : d->wall) * k; r->wall = d->wall * k;
        r->delay = -1; r->ivcs = r->vcs = -1;                    /* not split per stage */
    }
    printf("\n=== thread: %s (per run of each stage) ===", t->name);
    print_tables("stage", rows, m, 0);
    if (!t->watch) return;
    /* who used the core during each stage, as a share of the stage's wall time */
    printf("\n-- who used the core during each stage (share of wall time; others = other programs and the OS) --\n");
    for (int s = 0; s < n; s++) {
        if (!t->runs[s] || t->uwall[s] <= 0) continue;
        double w = t->uwall[s], ours = 0;
        printf("%-12s |", names[s]);
        for (int u = 0; u < t->un[s]; u++) {
            if (strcmp(t->uname[s][u], "idle")) ours += t->uns[s][u];
            if (t->uns[s][u] >= 0.005 * w) printf(" %s %.0f%% |", t->uname[s][u], 100 * t->uns[s][u] / w);
        }
        if (t->uidle[s] >= 0) {                                   /* core's idle time known */
            double others = w - t->uidle[s] - ours;
            printf(" others %.0f%% | idle %.0f%% |", 100 * (others > 0 ? others : 0) / w, 100 * t->uidle[s] / w);
        }
        printf("\n");
    }
}
void pmu_report(const char *names[], int n) {
    for (int i = 0; i < nthreads; i++) {
        int any = 0; for (int s = 0; s < n; s++) any |= threads[i].runs[s] > 0;
        if (any) report_thread(&threads[i], names, n);
    }
}

/* ============== RUN MODE (Linux): profile ANY program, every thread, unchanged ==============
 * pmu_run(argv) starts the program under ptrace. Linux stops each new thread before its first
 * instruction, so the counters are attached to every thread from its birth, whatever the
 * program does. When a thread exits, its counters and the kernel's scheduling numbers for it
 * are read. Nothing in the program has to change.                                           */
#if defined(__linux__)
#include <signal.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#define MAXR 256
typedef struct { pid_t tid; int fd[NC], tck; uint64_t born; int done; row_t r; } trk_t;
static trk_t trk[MAXR]; static int ntrk;

static void trk_add(pid_t tid) {                   /* new thread: counters on before it runs */
    if (ntrk == MAXR) return;
    trk_t *t = &trk[ntrk++]; memset(t, 0, sizeof *t); t->tid = tid; t->born = now_ns();
    for (int k = 0; k < NC; k++) t->fd[k] = have[k] ? perf_open_tid(ev[k].type, ev[k].cfg, tid, 0) : -1;
    t->tck = perf_open_tid(PERF_TYPE_SOFTWARE, PERF_COUNT_SW_TASK_CLOCK, tid, 1);  /* ns on a core */
}
static void trk_end(pid_t pid, pid_t tid) {        /* thread about to exit: read everything */
    trk_t *t = NULL; char p[128];
    for (int i = 0; i < ntrk; i++) if (trk[i].tid == tid && !trk[i].done) t = &trk[i];
    if (!t) return;
    row_t *r = &t->r;
    r->wall = (double)(now_ns() - t->born);
    for (int k = 0; k < NC; k++) { r->c[k] = (double)perf_value(t->fd[k]); if (t->fd[k] >= 0) close(t->fd[k]); }
    r->run = (double)perf_value(t->tck); if (t->tck >= 0) close(t->tck);
    snprintf(p, sizeof p, "/proc/%d/task/%d/schedstat", pid, tid);          /* ran ns, waited-to-run ns */
    FILE *f = fopen(p, "r"); unsigned long long ran = 0, wait = 0;
    if (f) { if (fscanf(f, "%llu %llu", &ran, &wait) != 2) ran = wait = 0; fclose(f); }
    r->delay = ran > 0 ? (double)wait : -1;                        /* 0 0: kernel does not record it */
    if (r->run <= 0) r->run = (double)ran;
    snprintf(p, sizeof p, "/proc/%d/task/%d/status", pid, tid);             /* name, switch counts */
    if ((f = fopen(p, "r"))) {
        char line[128];
        while (fgets(line, sizeof line, f)) {
            if (!strncmp(line, "Name:", 5)) sscanf(line + 5, "%15s", r->name);
            sscanf(line, "voluntary_ctxt_switches: %ld", &r->vcs);
            sscanf(line, "nonvoluntary_ctxt_switches: %ld", &r->ivcs);
        }
        fclose(f);
    }
    t->done = 1;
}
static void report_run(void) {
    static row_t rows[MAXR + 1]; int n = 0; row_t all; memset(&all, 0, sizeof all);
    snprintf(all.name, 16, "ALL");
    for (int i = 0; i < ntrk; i++) {
        if (!trk[i].done) continue;
        row_t *r = &trk[i].r; rows[n++] = *r;                      /* one row per thread */
        for (int k = 0; k < NC; k++) all.c[k] += r->c[k];          /* and the whole program */
        all.run += r->run; all.wall += r->wall; all.ivcs += r->ivcs; all.vcs += r->vcs;
        all.delay = (all.delay < 0 || r->delay < 0) ? -1 : all.delay + r->delay;
    }
    rows[n++] = all;
    print_tables("thread", rows, n, 1);
    printf("(ALL = the threads added up: a program can use several cores at once, so its time can\n"
           " exceed the run time; - = not counted, or the kernel does not split waiting)\n");
}

int pmu_run(char *argv[]) {
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {                                                /* the program to profile */
        ptrace(PTRACE_TRACEME, 0, NULL, NULL); raise(SIGSTOP);
        execvp(argv[0], argv); perror(argv[0]); _exit(127);
    }
    int st; waitpid(pid, &st, 0);
    ptrace(PTRACE_SETOPTIONS, pid, NULL, (void *)(long)(PTRACE_O_TRACECLONE | PTRACE_O_TRACEEXEC |
           PTRACE_O_TRACEEXIT | PTRACE_O_EXITKILL));
    ptrace(PTRACE_CONT, pid, NULL, NULL);
    uint64_t t0 = 0, t1 = 0; int code = 0;
    for (;;) {
        pid_t w = waitpid(-1, &st, __WALL);
        if (w < 0) break;                                          /* no threads left */
        if (WIFEXITED(st) || WIFSIGNALED(st)) {
            if (w == pid) { t1 = now_ns(); code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st); }
            continue;
        }
        if (!WIFSTOPPED(st)) continue;
        int sig = WSTOPSIG(st), evt = st >> 16, fwd = 0;
        if (sig == SIGTRAP && evt == PTRACE_EVENT_EXEC) { t0 = now_ns(); trk_add(pid); }   /* program starts */
        else if (sig == SIGTRAP && evt == PTRACE_EVENT_CLONE) {    /* new thread: still stopped */
            unsigned long nt; ptrace(PTRACE_GETEVENTMSG, w, NULL, &nt); trk_add((pid_t)nt);
        }
        else if (sig == SIGTRAP && evt == PTRACE_EVENT_EXIT) trk_end(pid, w);
        else if (sig == SIGSTOP && evt == 0) fwd = 0;              /* a new thread's first stop */
        else if (evt == 0) fwd = sig;                              /* the program's own signal: pass it on */
        ptrace(PTRACE_CONT, w, NULL, (void *)(long)fwd);
    }
    if (!t0) { printf("\nprogram %s did not start (exit code %d)\n", argv[0], code); return code ? code : 1; }
    printf("\n=== program: %s   exit code %d   run time %.1f ms   threads %d ===\n", argv[0], code, (t1 - t0) / 1e6, ntrk);
    report_run();
    return code;
}
#else
int pmu_run(char *argv[]) { (void)argv; printf("pmu_run: only on Linux; use pmu_profile()\n"); return -1; }
#endif

/* profile a function (all boards; the way to run "any code" on a microcontroller):
 * the whole call is one stage, and the report lists every task that used the core */
void pmu_profile(const char *name, void (*fn)(void)) {
    const char *names[1] = { name };
    pmu_begin(0); fn(); pmu_end(0);
    pmu_report(names, 1);
}
