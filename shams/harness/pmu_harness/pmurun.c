/* pmurun - profile any program, unchanged, on a Linux board:
 *     ./pmurun ./program [arguments]
 * 1. hardware sweep (this board's prices), 2. run the program, following every thread,
 * 3. print what each thread did and where its time went. */
#include "pmu.h"
#include <stdio.h>
int main(int argc, char *argv[]) {
    if (argc < 2) { printf("usage: %s program [arguments]\n", argv[0]); return 1; }
    if (pmu_init() != 0) return 1;
    return pmu_run(argv + 1);
}
