/*
 * S4a probe: map offscreen VRAM into this process and prove that the user
 * mapping and the Storm engine see the same memory.
 * docs/S4A_VRAM_MMAP_PLAN.md.
 *
 * Build on the target:
 *   cc -O -Wall -o /tmp/osmga-vram openstep-mga-vram-mmap-probe.m -lDriver
 *
 * Run as root (the device node is 0600 root-owned):
 *   osmga-vram <window-offset-bytes>
 *
 * The major number is allocated dynamically by the kernel, so it MUST be read
 * from the driver (IOCharacterMajor) rather than hardcoded -- the same lesson
 * as Display0's object number changing between boots.  The node is created
 * here with mknod because OPENSTEP has no devfs and cdevsw registration does
 * not make a /dev entry.
 */

#import <driverkit/IODeviceMaster.h>
#import <driverkit/IODevice.h>
#import <driverkit/return.h>
#import <stdio.h>
#import <stdlib.h>
#import <string.h>
#import <fcntl.h>
#import <unistd.h>
#import <errno.h>
#import <sys/types.h>
#import <sys/stat.h>
#import <sys/mman.h>
#import <mach/mach.h>

/* This libc's headers do not declare these for us; munmap does not exist at
 * all, so the probe simply exits to release its mappings. */
extern int open(const char *, int, ...);
extern int close(int);
extern caddr_t mmap(caddr_t, int, int, int, int, off_t);

/*
 * This is the 4.2BSD mmap (mman.h is "7.1 Berkeley 6/4/86"): there is no
 * MAP_FIXED and no "pick an address for me".  _smmap first calls
 * vm_map_check_protection() on the address the caller supplies, then
 * vm_deallocate()s that range and maps the device object in its place.  So the
 * caller must ALREADY own readable/writable address space at that address --
 * passing 0 fails with EINVAL.  We therefore vm_allocate a placeholder first
 * and map over it.
 */
static caddr_t
mapDevice(int fd, unsigned long offset, int len)
{
    vm_address_t addr = 0;
    kern_return_t kr;

    kr = vm_allocate(task_self(), &addr, (vm_size_t)len, TRUE);
    if (kr != KERN_SUCCESS) {
        printf("   vm_allocate failed kr=%d\n", (int)kr);
        return (caddr_t)-1;
    }
    /* This mmap maps AT the supplied address and returns 0 on success -- it
     * does not return the address.  So check for failure, then hand back the
     * address we reserved. */
    if ((int)mmap((caddr_t)addr, len, PROT_READ | PROT_WRITE, MAP_SHARED,
                  fd, (off_t)offset) == -1)
        return (caddr_t)-1;
    return (caddr_t)addr;
}

#define DEV_PATH      "/dev/osmgavram"
#define PROBE_PARAM   "OSMGAProbeBlit"
#define FILL_PARAM    "OSMGAProbeFill"
#define MAP_LEN       (64 * 1024)          /* 16 pages */

static unsigned
pattern(unsigned i)
{
    return 0xA5000000U | (i & 0x00FFFFFFU);
}

int
main(int argc, char **argv)
{
    IODeviceMaster *master;
    IOObjectNumber objNum = 0;
    IOString kind;
    unsigned major = 0;
    unsigned count;
    IOReturn r;
    unsigned long winOff;
    int fd;
    volatile unsigned *p;
    unsigned i, bad;

    winOff = (argc >= 2) ? strtoul(argv[1], (char **)0, 0)
                         : (4UL * 1024 * 1024);   /* default: window start */

    setbuf(stdout, (char *)0);   /* unbuffered: survive a fault */

    master = [IODeviceMaster new];
    if (master == nil) { printf("no device master\n"); return 1; }
    r = [master lookUpByDeviceName:"Display0" objectNumber:&objNum
                        deviceKind:&kind];
    if (r != IO_R_SUCCESS) { printf("Display0 lookup failed r=%d\n", (int)r); return 1; }

    count = 1;
    r = [master getIntValues:&major forParameter:IO_CHARACTER_MAJOR
                objectNumber:objNum count:&count];
    if (r != IO_R_SUCCESS || count != 1) {
        printf("OSMGA_VRAM result=no-major r=%d (driver did not register the "
               "character device?)\n", (int)r);
        return 1;
    }
    printf("character major = %u\n", major);

    (void)unlink(DEV_PATH);
    if (mknod(DEV_PATH, S_IFCHR | 0600, (int)((major << 8) | 0)) != 0) {
        printf("mknod %s failed errno=%d\n", DEV_PATH, errno);
        return 1;
    }

    fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) { printf("open failed errno=%d\n", errno); return 1; }

    p = (volatile unsigned *)mapDevice(fd, winOff, MAP_LEN);
    if (p == (volatile unsigned *)-1) {
        printf("OSMGA_VRAM_MMAP offset=%lu result=FAILED errno=%d\n",
               winOff, errno);
        close(fd);
        return 1;
    }
    printf("OSMGA_VRAM_MMAP offset=%lu len=%d result=OK addr=%p\n",
           winOff, MAP_LEN, (void *)p);

    /* 1. userspace writes, userspace reads back (mapping itself sane) */
    for (i = 0; i < MAP_LEN / 4; i++)
        p[i] = pattern(i);
    bad = 0;
    for (i = 0; i < MAP_LEN / 4; i++)
        if (p[i] != pattern(i)) bad++;
    printf("OSMGA_VRAM_SELF  %s (%u bad)\n", bad ? "FAIL" : "PASS", bad);

    /*
     * 2. STALE-CACHE TEST -- the question that gates Mesa.
     * Fill the mapped region from userspace, read it so the values are in
     * this process's cache, then have the ENGINE overwrite the same VRAM,
     * then read again.  If the user mapping is write-back cached and nothing
     * invalidates it, we still see the old values.  A plain round trip would
     * not detect that; this does.
     */
    {
        unsigned long stride = 1024;              /* 1024x768x32 test mode */
        unsigned long y0 = winOff / (stride * 4); /* window start row */
        unsigned fillColour = 0x5AA55AA5U;
        unsigned f[5];
        unsigned beforeA, beforeB, afterA, afterB;

        for (i = 0; i < MAP_LEN / 4; i++)
            p[i] = pattern(i);
        beforeA = p[0];
        beforeB = p[(stride * 4) / 4];            /* start of the next row */

        f[0] = 0;                 /* x */
        f[1] = (unsigned)y0;      /* y: first row of the window */
        f[2] = 64;                /* w */
        f[3] = 8;                 /* h */
        f[4] = fillColour;
        r = [master setIntValues:f forParameter:FILL_PARAM
                    objectNumber:objNum count:5];
        printf("OSMGA_VRAM_FILL y=%lu colour=%08x r=%d %s\n",
               y0, fillColour, (int)r,
               (r == IO_R_SUCCESS) ? "OK" : "REFUSED/FAILED");

        if (r == IO_R_SUCCESS) {
            afterA = p[0];
            afterB = p[(stride * 4) / 4];
            printf("OSMGA_VRAM_CACHE row0 before=%08x after=%08x -> %s\n",
                   beforeA, afterA,
                   (afterA == fillColour) ? "COHERENT"
                     : (afterA == beforeA ? "*** STALE (cached) ***"
                                          : "*** UNEXPECTED ***"));
            printf("OSMGA_VRAM_CACHE row1 before=%08x after=%08x -> %s\n",
                   beforeB, afterB,
                   (afterB == fillColour) ? "COHERENT"
                     : (afterB == beforeB ? "*** STALE (cached) ***"
                                          : "*** UNEXPECTED ***"));
            /* reverse direction: does the engine see what the CPU wrote?
             * verified indirectly -- the fill overwrote CPU-written data, so
             * a later CPU write followed by a fill of a DIFFERENT area lets
             * us check the untouched part is still the CPU pattern. */
            /* The fill is 64 px wide over 8 rows of stride 1024, so it
             * touches [row*1024, row*1024+64) -- NOT a contiguous 512 words.
             * Check stride-aware, or the test lies. */
            bad = 0;
            for (i = 0; i < MAP_LEN / 4; i++) {
                unsigned row = i / 1024, col = i % 1024;
                unsigned inFill = (row < 8 && col < 64);
                unsigned want = inFill ? fillColour : pattern(i);
                if (p[i] != want) {
                    bad++;
                    if (bad < 3)
                        printf("   mismatch at %u (r%u c%u): %08x want %08x\n",
                               i, row, col, p[i], want);
                }
            }
            printf("OSMGA_VRAM_UNTOUCHED %s (%u differ outside the filled "
                   "rect)\n", bad ? "FAIL" : "PASS", bad);
        }
    }

    /* 3. out-of-window offsets must be refused */
    {
        volatile unsigned *bad1;
        bad1 = (volatile unsigned *)mapDevice(fd, 0UL, 8192);
        printf("OSMGA_VRAM_GUARD offset=0 (visible scanout) -> %s\n",
               (bad1 == (volatile unsigned *)-1) ? "REFUSED (correct)"
                                                 : "*** MAPPED - BUG ***");

        bad1 = (volatile unsigned *)mapDevice(fd, 8UL * 1024 * 1024, 8192);
        printf("OSMGA_VRAM_GUARD offset=8MiB (beyond proven VRAM) -> %s\n",
               (bad1 == (volatile unsigned *)-1) ? "REFUSED (correct)"
                                                 : "*** MAPPED - BUG ***");
    }

    close(fd);
    return 0;
}
