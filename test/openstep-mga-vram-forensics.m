/*
 * Forensics for a VRAM interface that has stopped returning what was
 * written.  Userland only: it maps the offscreen window the driver already
 * exposes and needs no driver change, which matters because the machine
 * cannot be rebooted without losing the state under investigation.
 *
 *   vramforensics <window-offset-bytes> <dump-path>
 *
 * Three passes, because "the memory is broken" is not a finding:
 *
 *   A  write an address-derived pattern, read it back    (is it wrong?)
 *   B  write a SECOND pattern, read back                 (writes or reads?)
 *      -- if B reads back as A, writes are being lost
 *      -- if B reads back as neither, reads are unreliable
 *   C  read the same location repeatedly without writing (is it stable?)
 *      -- unstable means the read path, stable means the stored value
 *
 * The raw pairs go to a file so the analysis happens off-machine; guessing
 * at structure from a count of bad words is how a lane fault gets recorded
 * as "memory corrupted".
 *
 * Strict C89 plus Objective-C -- NeXT cc 2.7.2.1.
 */
#import <driverkit/IODeviceMaster.h>
#import <driverkit/IODevice.h>
#import <driverkit/return.h>
#import <stdio.h>
#import <string.h>
#import <stdlib.h>
#import <errno.h>
#import <fcntl.h>
#import <sys/types.h>
#import <sys/stat.h>
#import <sys/mman.h>
#import <mach/mach.h>

extern int open(const char *, int, ...);
extern int close(int);
extern int write();
extern int fsync();
extern int unlink(const char *);
extern int mknod(const char *, int, int);
extern caddr_t mmap(caddr_t, int, int, int, int, off_t);

#define DEV_PATH  "/dev/osmgavramf"
#define MAP_LEN   65536
#define NWORDS    (MAP_LEN / 4)

static caddr_t
mapDevice(int fd, unsigned long offset, int len)
{
    vm_address_t addr = 0;
    if (vm_allocate(task_self(), &addr, (vm_size_t)len, TRUE) != KERN_SUCCESS)
        return (caddr_t)-1;
    if ((int)mmap((caddr_t)addr, len, PROT_READ | PROT_WRITE, MAP_SHARED,
                  fd, (off_t)offset) == -1)
        return (caddr_t)-1;
    return (caddr_t)addr;
}

static unsigned patA(i) int i; { return (unsigned)(0xA5000000UL | (unsigned long)i); }
static unsigned patB(i) int i; { return (unsigned)(0x5C000000UL | (unsigned long)(i ^ 0xFFFF)); }

int
main(int argc, char **argv)
{
    IODeviceMaster *master;
    IOObjectNumber objNum;
    IOString kind;
    unsigned major, count;
    volatile unsigned *p;
    unsigned *gotA, *gotB;
    int fd, out, i, badA, badB, stuck;
    unsigned long winOff;
    char line[160];

    if (argc < 3) { fprintf(stderr, "usage: vramforensics <offset> <dump>\n"); return 2; }
    winOff = (unsigned long)strtoul(argv[1], (char **)0, 0);

    gotA = (unsigned *)malloc(NWORDS * sizeof(unsigned));
    gotB = (unsigned *)malloc(NWORDS * sizeof(unsigned));
    if (!gotA || !gotB) { fprintf(stderr, "out of memory\n"); return 2; }

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
                        deviceKind:&kind] != IO_R_SUCCESS) {
        printf("Display0 lookup failed\n"); return 1;
    }
    count = 1;
    if ([master getIntValues:&major forParameter:IO_CHARACTER_MAJOR
                objectNumber:objNum count:&count] != IO_R_SUCCESS) {
        printf("no character major\n"); return 1;
    }
    (void)unlink(DEV_PATH);
    if (mknod(DEV_PATH, S_IFCHR | 0600, (int)((major << 8) | 0)) != 0) {
        printf("mknod failed errno=%d\n", errno); return 1;
    }
    fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) { printf("open failed errno=%d\n", errno); return 1; }

    p = (volatile unsigned *)mapDevice(fd, winOff, MAP_LEN);
    if (p == (volatile unsigned *)-1) {
        printf("map failed errno=%d\n", errno); return 1;
    }
    printf("mapped offset=%lu len=%d at %p\n", winOff, MAP_LEN, (void *)p);

    /* pass A */
    for (i = 0; i < NWORDS; i++) p[i] = patA(i);
    for (i = 0; i < NWORDS; i++) gotA[i] = p[i];
    badA = 0;
    for (i = 0; i < NWORDS; i++) if (gotA[i] != patA(i)) badA++;

    /* pass B -- a different pattern over the same words */
    for (i = 0; i < NWORDS; i++) p[i] = patB(i);
    for (i = 0; i < NWORDS; i++) gotB[i] = p[i];
    badB = 0;
    for (i = 0; i < NWORDS; i++) if (gotB[i] != patB(i)) badB++;

    /* pass C -- re-read without writing; does the same address answer the
     * same way twice? */
    stuck = 0;
    for (i = 0; i < NWORDS; i++) if (p[i] != gotB[i]) stuck++;

    printf("A: %d/%d words wrong\n", badA, NWORDS);
    printf("B: %d/%d words wrong\n", badB, NWORDS);
    printf("C: %d/%d words changed on a second read with no write\n", stuck, NWORDS);

    out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { printf("cannot create %s\n", argv[2]); return 1; }
    sprintf(line, "# idx expectA gotA expectB gotB\n");
    write(out, line, strlen(line));
    for (i = 0; i < NWORDS; i++) {
        sprintf(line, "%d %08x %08x %08x %08x\n",
                i, patA(i), gotA[i], patB(i), gotB[i]);
        write(out, line, strlen(line));
    }
    fsync(out);
    close(out);
    printf("dumped %d words to %s\n", NWORDS, argv[2]);
    return 0;
}
