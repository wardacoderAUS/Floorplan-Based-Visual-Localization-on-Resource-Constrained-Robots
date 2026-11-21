/* mcu_main.c - microcontrollers cannot launch a separate program, so the code to profile is
 * compiled together with the harness and run inside pmu_profile(). Replace workload_main
 * with any code: the report lists every task that used the core, and what the core did. */
#include "pmu.h"
int workload_main(void);                              /* the code to profile (workload.c) */
static void run(void) { workload_main(); }
#if defined(ESP_PLATFORM)
void app_main(void) { if (pmu_init() == 0) pmu_profile("workload", run); }
#else                                                 /* GAP9 (PMSIS) */
#include "pmsis.h"
static void entry(void) { if (pmu_init() == 0) pmu_profile("workload", run); pmsis_exit(0); }
int main(void) { return pmsis_kickoff((void *)entry); }
#endif
