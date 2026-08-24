/*
 * What a word of video memory costs, written and read, through the mapping
 * the probes use.
 *
 * The frame work measured the READ rate by timing a whole-surface mirror --
 * 5.36 MB/s -- and the WRITE rate only indirectly, as the difference between
 * two clears.  Neither number was ever taken on its own, and one of the open
 * questions needs both: a probe that spends most of an hour in one section
 * is either doing an enormous amount of bus traffic or waiting on something,
 * and the two look nothing alike once the rates are known.
 *
 * It writes into the offscreen window the other probes use and reads it back.
 * Nothing on the visible screen is touched.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <mach/mach.h>
#include "OpenStepMGAHW3D.h"

extern int open(const char *, int, ...);
extern int close(int);
extern char *mmap(char *, int, int, int, int, long);

#define DEV     "/dev/osmgavram"
#define WINOFF  4194304UL               /* the offscreen window's start */
#define WORDS   65536UL                 /* 64 rows of 1024, what blank() does */

static double
now(void)
{
    struct timeval t;
    gettimeofday(&t, (struct timezone *)0);
    return (double)t.tv_sec + (double)t.tv_usec / 1e6;
}

int
main(void)
{
    int fd;
    vm_address_t addr = 0;
    volatile unsigned long *p;
    unsigned long bytes = WORDS * 4UL;
    double t0, t1, wr, rd;
    unsigned long i, sink = 0UL;

    fd = open(DEV, 2 /* O_RDWR */);
    if (fd < 0) { printf("no device\n"); return 2; }
    if (vm_allocate(task_self(), &addr, (vm_size_t)bytes, TRUE)
            != KERN_SUCCESS) {
        printf("no room\n"); return 2;
    }
    if ((int)mmap((char *)addr, (int)bytes, 3, 1, fd, (long)WINOFF) == -1) {
        printf("the window will not map\n"); return 2;
    }
    p = (volatile unsigned long *)addr;

    /* Touch it once so nothing here is measuring a first fault. */
    for (i = 0UL; i < WORDS; i++) p[i] = 0UL;

    t0 = now();
    for (i = 0UL; i < WORDS; i++)
        p[i] = 0x11223344UL;
    t1 = now();
    wr = t1 - t0;

    t0 = now();
    for (i = 0UL; i < WORDS; i++)
        sink += p[i];
    t1 = now();
    rd = t1 - t0;

    printf("video memory, through the client's mapping\n\n");
    printf("   %lu words (%lu bytes), one at a time\n\n", WORDS, bytes);
    printf("   write  %8.3f ms   %7.2f MB/s   %6.2f us a word\n",
           wr * 1000.0, bytes / wr / 1e6, wr * 1e6 / (double)WORDS);
    printf("   read   %8.3f ms   %7.2f MB/s   %6.2f us a word\n",
           rd * 1000.0, bytes / rd / 1e6, rd * 1e6 / (double)WORDS);
    printf("\n   (sink %lu, so the reads cannot be optimised away)\n", sink);
    (void)close(fd);
    return 0;
}
