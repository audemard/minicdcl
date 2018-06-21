#include "utils/System.h"

#if defined(__linux__)

#include <stdio.h>
#include <stdlib.h>

using namespace CDCL;

// TODO: split the memory reading functions into two: one for reading high-watermark of RSS, and
// one for reading the current virtual memory size.

static inline int memReadStat(int field)
{
    char  name[256];
    pid_t pid = getpid();
    int   value;

    sprintf(name, "/proc/%d/statm", pid);
    FILE* in = fopen(name, "rb");
    if (in == NULL) return 0;

    for (; field >= 0; field--)
        if (fscanf(in, "%d", &value) != 1)
            printf("ERROR! Failed to parse memory statistics from \"/proc\".\n"), exit(1);
    fclose(in);
    return value;
}


static inline int memReadPeak(void)
{
    char  name[256];
    pid_t pid = getpid();

    sprintf(name, "/proc/%d/status", pid);
    FILE* in = fopen(name, "rb");
    if (in == NULL) return 0;

    // Find the correct line, beginning with "VmPeak:":
    int peak_kb = 0;
    while (!feof(in) && fscanf(in, "VmPeak: %d kB", &peak_kb) != 1)
        while (!feof(in) && fgetc(in) != '\n')
            ;
    fclose(in);

    return peak_kb;
}

double CDCL::memUsed() { return (double)memReadStat(0) * (double)getpagesize() / (1024*1024); }
double CDCL::memUsedPeak() {
    double peak = memReadPeak() / 1024;
    return peak == 0 ? memUsed() : peak; }

#elif defined(__FreeBSD__)

double CDCL::memUsed(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return (double)ru.ru_maxrss / 1024; }
double MiniSat::memUsedPeak(void) { return memUsed(); }


#elif defined(__APPLE__)
#include <malloc/malloc.h>

double CDCL::memUsed(void) {
    malloc_statistics_t t;
    malloc_zone_statistics(NULL, &t);
    return (double)t.max_size_in_use / (1024*1024); }

#else
double CDCL::memUsed() {
    return 0; }
#endif
