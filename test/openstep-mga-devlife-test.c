/*
 * Does this kernel tell the driver when a client goes away?
 *
 * The device has no per-client state at all: open is given a device number
 * and two flags, close is given the same and ignores them, and the submit
 * ioctl is given (dev, cmd, data, flag).  None of them is told who is
 * asking.  So two accelerated processes share one batch buffer and one
 * video-memory surface with nothing arbitrating between them.
 *
 * Letting only one client in at a time is the obvious answer, and it rests
 * entirely on close being called when a client goes away -- especially when
 * it is KILLED rather than exiting.  Nothing in this tree establishes that;
 * the disassembly covers mmap and the VM object, not the close path.  An
 * exclusive open that is never released would be worse than the problem:
 * today a crashed client costs nothing, and then it would cost acceleration
 * until the machine was rebooted.
 *
 * So this asks, one case at a time, and the driver counts on the other side.
 * Aggregate counts cannot tell these cases apart, which is why each is a
 * separate run with its own marker: read /usr/adm/messages between runs.
 *
 * It opens and closes and nothing else -- no mmap, no ioctl, no submission.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
/* This libc's headers declare none of these where we can see them. */
extern int kill(int, int);
extern int close(int);
extern int fork(void);
extern int getpid(void);
extern unsigned int sleep(unsigned int);
extern int open(const char *, int, ...);
extern char *mmap(char *, int, int, int, int, long);
#include <unistd.h>
#include <fcntl.h>
#include <mach/mach.h>

#define DEV "/dev/osmgavram"

static int
opendev(const char *what)
{
    int fd = open(DEV, O_RDWR);

    printf("   %-18s open -> fd %d%s\n", what, fd,
           (fd < 0) ? "  (REFUSED)" : "");
    fflush(stdout);
    return fd;
}

static void
usage(void)
{
    printf("usage: tdl <case>\n\n");
    printf("   plain        open, pause, close, pause\n");
    printf("   twice        open twice in one process, close both\n");
    printf("   exitnoclose  open and exit(0) without closing\n");
    printf("   forkchild    open, fork, child exits at once, parent closes\n");
    printf("   forkboth     open, fork, neither closes, both exit\n");
    printf("   kill         open, then SIGKILL this process\n");
    printf("   map          open, MAP the command window, exit without"
           " unmapping\n");
    printf("   mapunmap     open, map, unmap, close, exit\n");
    printf("\n   read /usr/adm/messages after each run; the driver counts\n"
           "   opens and closes and prints both every time.\n");
}

int
main(int argc, char **argv)
{
    int fd, fd2;

    if (argc < 2) { usage(); return 2; }

    printf("device lifetime: case \"%s\", pid %d\n\n", argv[1], (int)getpid());

    if (strcmp(argv[1], "plain") == 0) {
        fd = opendev("plain");
        if (fd < 0) return 1;
        sleep(1);
        close(fd);
        printf("   closed\n");
        sleep(1);
        return 0;
    }
    if (strcmp(argv[1], "twice") == 0) {
        fd  = opendev("first");
        fd2 = opendev("second");
        if (fd >= 0) close(fd);
        if (fd2 >= 0) close(fd2);
        printf("   closed both\n");
        return 0;
    }
    if (strcmp(argv[1], "exitnoclose") == 0) {
        fd = opendev("exitnoclose");
        if (fd < 0) return 1;
        printf("   exiting WITHOUT closing\n");
        fflush(stdout);
        return 0;                       /* exit(0) with the fd still open */
    }
    if (strcmp(argv[1], "forkchild") == 0) {
        int pid;

        fd = opendev("forkchild");
        if (fd < 0) return 1;
        pid = fork();
        if (pid == 0) {
            printf("   child %d exiting, fd inherited and not closed\n",
                   (int)getpid());
            fflush(stdout);
            _exit(0);
        }
        sleep(2);                       /* let the child go first */
        printf("   parent closing\n");
        fflush(stdout);
        close(fd);
        sleep(1);
        return 0;
    }
    if (strcmp(argv[1], "forkboth") == 0) {
        int pid;

        fd = opendev("forkboth");
        if (fd < 0) return 1;
        pid = fork();
        if (pid == 0) { _exit(0); }
        sleep(2);
        printf("   parent exiting without closing either\n");
        fflush(stdout);
        return 0;
    }
    if (strcmp(argv[1], "kill") == 0) {
        fd = opendev("kill");
        if (fd < 0) return 1;
        printf("   killing this process with the fd open\n");
        fflush(stdout);
        sleep(1);
        kill(getpid(), SIGKILL);
        printf("   STILL ALIVE -- the kill did not work\n");
        return 1;
    }

    if (strcmp(argv[1], "map") == 0 || strcmp(argv[1], "mapunmap") == 0) {
        /*
         * The one that matters.  Every real client MAPS this device, and the
         * plain cases above do not -- and the regression, whose programs all
         * map, produced fifteen opens and one close while four programs that
         * only open and exit produced four of each.  A mapping holds a VM
         * object reference on the device, and if that reference outlives the
         * descriptor then close does not arrive when the client goes away.
         *
         * PROT_READ|PROT_WRITE because the handler refuses anything else.
         * Nothing is read and nothing is written: the question is whether the
         * mapping exists, not what is in it.
         *
         * 4.2BSD mmap has no MAP_FIXED and no "pick an address": it maps over
         * a range the caller already owns, so the placeholder is allocated
         * first, exactly as the Mesa client does it.
         */
        vm_address_t addr = 0;
        int len = 24576;                /* the batch window, three pages */

        fd = opendev(argv[1]);
        if (fd < 0) return 1;
        if (vm_allocate(task_self(), &addr, (vm_size_t)len, TRUE)
                != KERN_SUCCESS) {
            printf("   no room\n");
            return 2;
        }
        if ((int)mmap((char *)addr, len, 3 /* RW */, 1 /* MAP_SHARED */,
                      fd, 0x40000000L) == -1) {
            printf("   mmap REFUSED -- this case says nothing\n");
            close(fd);
            return 2;
        }
        printf("   mapped %d bytes of the command window at %p\n", len,
               (void *)addr);
        fflush(stdout);
        if (strcmp(argv[1], "mapunmap") == 0) {
            (void)vm_deallocate(task_self(), addr, (vm_size_t)len);
            close(fd);
            printf("   unmapped and closed\n");
            fflush(stdout);
            sleep(1);
            return 0;
        }
        printf("   exiting with the mapping still in place\n");
        fflush(stdout);
        return 0;
    }

    usage();
    return 2;
}
