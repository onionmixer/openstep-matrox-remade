/*
 * T0.1 -- one read-only snapshot of the registers a soft reset might
 * disturb, written where a freeze cannot take it.
 *
 *   regsnap <label> [outfile]
 *
 * The point of the file is T1.  A soft reset "returns some register bits to
 * their soft-reset values (see individual registers)" (G400 spec 3-167) and
 * that sentence appears exactly once in 690 pages, with no individual
 * register documenting such a value.  So the PRE/POST difference has to be
 * measured, and this is what measures it.
 *
 * Every register read here is R/W or RO in the specification's index.  That
 * is not a formality: of the 217 registers the index lists, 161 are
 * write-only, and the first draft of the test design asked for three of
 * them (DWGCTL, WIADDR2, WIADDRNB2) by name.
 *
 * The file is opened once and fsynced after the record, per the rule in
 * W10 1.3: reopening to append on this NFS mount loses everything but the
 * last record.  Each run writes its own file.  Nothing here writes to the
 * card.
 *
 * Strict C89 plus Objective-C -- NeXT cc 2.7.2.1.
 */
#import <driverkit/IODeviceMaster.h>
#import <driverkit/return.h>
#import <stdio.h>
#import <string.h>
#import <fcntl.h>
#import <sys/time.h>

extern int open(); extern int write(); extern int close(); extern int fsync();

#define SNAP_PARAM  "OSMGARegSnapshot"
#define SNAP_COUNT  25U

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
    if (outFd >= 0) {
        write(outFd, line, strlen(line));
        fsync(outFd);
    }
}

int
main(int argc, char **argv)
{
    IODeviceMaster *master;
    IOObjectNumber objNum;
    IOString kind;
    unsigned v[SNAP_COUNT], n = SNAP_COUNT;
    unsigned i;
    struct timeval tv;
    char line[256];
    char *label = (argc > 1) ? argv[1] : "snap";

    if (argc > 2) {
        outFd = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (outFd < 0) {
            fprintf(stderr, "cannot create %s\n", argv[2]);
            return 2;
        }
    }

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) {
        emit("regsnap: Display0 not found\n");
        return 1;
    }

    if ([master getIntValues:v forParameter:SNAP_PARAM
                objectNumber:objNum count:&n] != IO_R_SUCCESS) {
        emit("regsnap: OSMGARegSnapshot refused -- is the new driver loaded?\n");
        return 1;
    }
    if (n != SNAP_COUNT) {
        sprintf(line, "regsnap: got %u words, expected %u\n", n, SNAP_COUNT);
        emit(line);
        return 1;
    }

    gettimeofday(&tv, (struct timezone *)0);
    sprintf(line, "SNAP %s t=%ld.%06ld version=%u\n",
            label, (long)tv.tv_sec, (long)tv.tv_usec, v[0]);
    emit(line);

    for (i = 1U; i < SNAP_COUNT; i++) {
        sprintf(line, "  %-20s %08x\n", fieldName[i], v[i]);
        emit(line);
    }

    /* The two questions T0 exists to answer, spelled out rather than left
     * for the reader to decode from hex. */
    if (v[3] == 0U)
        emit("  >> RASTER: VCOUNT did NOT move in 2000 reads -- not scanning\n");
    else {
        sprintf(line, "  >> RASTER: scanning (VCOUNT moved after %u reads)\n", v[3]);
        emit(line);
    }

    if (v[23] == 0xFFFFFFFFU)
        emit("  >> ABORTS: configuration space unavailable (probe did not record us)\n");
    else {
        sprintf(line, "  >> ABORTS: recmastab<29>=%u rectargab<28>=%u  %s\n",
                (v[23] >> 29) & 1U, (v[23] >> 28) & 1U,
                (((v[23] >> 28) & 3U) == 0U)
                    ? "both clear -- T2's observation would be meaningful"
                    : "ALREADY SET -- clear before T2 or its result means nothing");
        emit(line);
    }

    if (outFd >= 0)
        close(outFd);
    return 0;
}
