/* Which (offset, length) pairs does this device accept, and where do they land? */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <mach/mach.h>

#define DEV     "/dev/osmgavram"
#define WS      4194304UL

int
main(void)
{
    static unsigned long offs[] = { 0UL, 4096UL, 8192UL, 12288UL, 16384UL };
    static unsigned long lens[] = { 8192UL, 65536UL, 487424UL, 491520UL };
    int fd, i, j;

    if ((fd = open(DEV, O_RDWR)) < 0) { printf("no %s\n", DEV); return 1; }
    printf("%-10s", "offset");
    for (j = 0; j < 4; j++) printf(" len=%-8lu", lens[j]);
    printf("\n");
    for (i = 0; i < 5; i++) {
        printf("+%-9lu", offs[i]);
        for (j = 0; j < 4; j++) {
            vm_address_t a = 0;
            int rc;

            if (vm_allocate(task_self(), &a, (vm_size_t)lens[j], TRUE)
                != KERN_SUCCESS) { printf(" %-12s", "vm_alloc"); continue; }
            errno = 0;
            rc = (int)mmap((caddr_t)a, (int)lens[j], PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd, (long)(WS + offs[i]));
            printf(" %-12s", rc == -1 ? strerror(errno) : "ok");
            (void)vm_deallocate(task_self(), a, (vm_size_t)lens[j]);
        }
        printf("\n");
    }
    return 0;
}
