/*
 * T1 -- soft-reset the drawing engine on the card driving this console, and
 * measure what the specification declines to say.
 *
 *   softreset dry  <breadcrumb-file>     the control: no RST write
 *   softreset real <breadcrumb-file>     the reset
 *   softreset rearm                      clear the permanent-disable latch
 *
 * RUN dry FIRST, LOOK AT IT, THEN run real.  They are separate invocations
 * on purpose: putting both in one run means real goes out before anyone has
 * seen whether dry passed.
 *
 * The breadcrumb is written and fsynced BEFORE the ioctl, because the whole
 * point is that the record survives the machine not surviving.  One fd,
 * opened once, never reopened -- reopening to append on this NFS mount loses
 * everything but the last record (W10 1.3).  Each run writes its own file.
 *
 * The dry run is not a formality.  Several of these registers are DYNAMIC
 * and move on their own, so "PRE != POST" does not mean "the reset did it".
 * Dry measures that noise floor; real is only readable against it.
 *
 * Strict C89 plus Objective-C -- NeXT cc 2.7.2.1.
 */
#import <driverkit/IODeviceMaster.h>
#import <driverkit/return.h>
#import <stdio.h>
#import <string.h>
#import <stdlib.h>
#import <fcntl.h>
#import <sys/time.h>

extern int open(); extern int write(); extern int close(); extern int fsync();

#define SNAP_PARAM   "OSMGARegSnapshot"
#define SNAP_COUNT   25U
#define RESET_PARAM  "OSMGASoftReset"
#define RSNAP_PARAM  "OSMGAResetSnap"
#define RSNAP_COUNT  (2U * SNAP_COUNT + 2U)
#define REARM_PARAM  "OSMGAAccelRearm"

static const char *fieldName[SNAP_COUNT] = {
    "version",
    "VCOUNT.first",     "VCOUNT.second",   "VCOUNT.readsToMove",
    "FIFOSTATUS.1e10",  "STATUS.1e14",     "IEN.1e1c",
    "RST.1e40",         "MEMRDBK.1e44",    "PRIMPTR.1e50",
    "OPMODE.1e54",      "PRIMADDRESS.1e58","PRIMEND.1e5c",
    "WIADDRNB.1e60",    "WFLAGNB.1e64",    "WCODEADDR.1e6c",
    "WMISC.1e70",       "SECADDRESS.2c40", "SECEND.2c44",
    "SOFTRAP.2c48",     "DWGSYNC.2c4c",    "SETUPADDRESS.2cd0",
    "SETUPEND.2cd4",    "cfg.DEVCTRL.04",  "cfg.OPTION.40"
};

static int outFd = -1;

static void
emit(char *line)
{
    printf("%s", line);
    fflush(stdout);
    if (outFd >= 0) {
        write(outFd, line, strlen(line));
        fsync(outFd);               /* the record is on the host before we go on */
    }
}

static void
stamp(char *buf, char *what)
{
    struct timeval tv;
    gettimeofday(&tv, (struct timezone *)0);
    sprintf(buf, "%s t=%ld.%06ld\n", what, (long)tv.tv_sec, (long)tv.tv_usec);
}

int
main(int argc, char **argv)
{
    IODeviceMaster *master;
    IOObjectNumber objNum;
    IOString kind;
    unsigned snap[SNAP_COUNT], n;
    unsigned rs[RSNAP_COUNT], rn;
    unsigned arg[1];
    unsigned i, changed;
    IOReturn r;
    char line[256];
    int real;

    if (argc < 2) {
        fprintf(stderr, "usage: softreset dry|real <breadcrumb> | rearm\n");
        return 2;
    }

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) {
        printf("Display0 not found\n");
        return 1;
    }

    if (strcmp(argv[1], "rearm") == 0) {
        arg[0] = 1U;
        r = [master setIntValues:arg forParameter:REARM_PARAM
                    objectNumber:objNum count:1U];
        printf("rearm: %s (%d)\n",
               (r == IO_R_SUCCESS) ? "ok" : "refused", (int)r);
        return (r == IO_R_SUCCESS) ? 0 : 1;
    }

    if (strcmp(argv[1], "dry") == 0)       real = 0;
    else if (strcmp(argv[1], "real") == 0) real = 1;
    else { fprintf(stderr, "first argument must be dry, real or rearm\n"); return 2; }

    if (argc < 3) {
        fprintf(stderr, "refusing to run without a breadcrumb file\n");
        return 2;
    }
    outFd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (outFd < 0) { fprintf(stderr, "cannot create %s\n", argv[2]); return 2; }

    /* ---- ARM: everything we know, on the host, before anything happens ---- */
    stamp(line, real ? "ARM softreset REAL" : "ARM softreset dry");
    emit(line);

    n = SNAP_COUNT;
    if ([master getIntValues:snap forParameter:SNAP_PARAM
                objectNumber:objNum count:&n] != IO_R_SUCCESS) {
        emit("  pre-arm snapshot REFUSED -- new driver not loaded, stopping\n");
        return 1;
    }
    for (i = 1U; i < SNAP_COUNT; i++) {
        sprintf(line, "  arm.%-20s %08x\n", fieldName[i], snap[i]);
        emit(line);
    }
    if (snap[3] == 0U)
        emit("  arm.RASTER not moving BEFORE the test -- stopping\n");
    if (snap[3] == 0U)
        return 1;

    /* ---- the step itself ---- */
    stamp(line, "ARM ioctl OSMGASoftReset");
    emit(line);

    arg[0] = real ? 1U : 0U;
    r = [master setIntValues:arg forParameter:RESET_PARAM
                objectNumber:objNum count:1U];

    sprintf(line, "DONE ioctl rc=%d %s\n", (int)r,
            (r == IO_R_SUCCESS) ? "" :
            (r == IO_R_BUSY) ? "(engine claimed by someone else)" :
                               "(refused)");
    emit(line);
    if (r != IO_R_SUCCESS) { close(outFd); return 1; }

    /* ---- what it saw ---- */
    rn = RSNAP_COUNT;
    if ([master getIntValues:rs forParameter:RSNAP_PARAM
                objectNumber:objNum count:&rn] != IO_R_SUCCESS) {
        emit("  PRE/POST readback REFUSED\n");
        close(outFd);
        return 1;
    }

    sprintf(line, "  RST.readBackWhileAsserted %08x  (ffffffff = dry, not read)\n",
            rs[2U * SNAP_COUNT + 0U]);
    emit(line);
    sprintf(line, "  wasReal %u\n", rs[2U * SNAP_COUNT + 1U]);
    emit(line);

    /* VCOUNT fields are the liveness witness, not a compared value: they are
     * expected to differ and say nothing about the reset. */
    emit("  --- PRE -> POST (VCOUNT excluded; it is meant to move) ---\n");
    changed = 0U;
    for (i = 4U; i < SNAP_COUNT; i++) {
        unsigned a = rs[i], b = rs[SNAP_COUNT + i];
        if (a != b) {
            changed++;
            sprintf(line, "  CHANGED %-20s %08x -> %08x\n", fieldName[i], a, b);
            emit(line);
        }
    }
    if (changed == 0U)
        emit("  (no register in the set changed)\n");

    sprintf(line, "  POST.RASTER readsToMove=%08x %s\n",
            rs[SNAP_COUNT + 3U],
            (rs[SNAP_COUNT + 3U] == 0U) ? "*** NOT SCANNING ***" : "scanning");
    emit(line);

    stamp(line, "END");
    emit(line);
    close(outFd);
    return 0;
}
