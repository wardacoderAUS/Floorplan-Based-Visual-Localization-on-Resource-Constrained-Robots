/* pmu.h - explain where each stage's time goes, for any code, on any board.
 * Same source on Linux boards (Jetson, Raspberry Pi), ESP32 / ESP32-P4 (ESP-IDF)
 * and GAP9 (GAP SDK / PMSIS). Each stage of the program is wrapped:
 *     pmu_init();  ...  pmu_begin(0); stage0(); pmu_end(0); ...  pmu_report(names, n); */
#ifndef PMU_H
#define PMU_H
#include <stddef.h>

int   pmu_init(void);                          /* find counters, calibrate this board  */
void  pmu_begin(int stage);                    /* stage = 0..15                        */
void  pmu_end(int stage);
void  pmu_report(const char *names[], int n);  /* print where the time went            */

/* helpers so test programs stay portable */
void  pmu_sleep_us(unsigned us);
void *pmu_big_buffer(size_t *bytes);           /* buffer in the board's slowest memory */

/* threads: each thread that marks stages is measured separately */
void  pmu_thread(const char *name);            /* register the calling thread (optional:  */
                                               /* pmu_begin registers unnamed threads)    */
void  pmu_thread_done(void);                   /* the calling thread stops being measured */
int   pmu_spawn(int n, const char *name, void (*fn)(int id));
                                               /* start n threads/tasks on THIS core,     */
                                               /* named name1..nameN, each running fn(id);*/
                                               /* returns how many started (0 on GAP9)    */
void  pmu_join(void);                          /* wait until they have all returned       */

/* profile ANY code */
int   pmu_run(char *argv[]);                   /* Linux: run a program unchanged, profile every
                                                  thread it creates (see pmurun.c)         */
void  pmu_profile(const char *name, void (*fn)(void));   /* any board: profile one call;
                                                  lists every task that used the core     */

#endif
