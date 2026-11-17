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

#endif
