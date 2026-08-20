/*
 * openstep-mga-hw3d-tex-client.m -- M1-2g: texture through the batch path.
 *
 * D3-4a proved the texture unit by MMIO: a 64x64 texture whose texels
 * encode their own coordinates mapped identically onto a destination
 * rectangle, 4096 of 4096.  This asks the same question of the batch path,
 * so the control was measured before the test was written.
 *
 * It also closes a hole rather than only adding a feature.  The batch
 * carried texture fields that the encoder never emitted, so a textured
 * batch would have drawn with whatever texture state was left behind --
 * the same inheritance that once made every pixel of an image differ.
 *
 *   cc -O -Wall -o /tmp/osmga-hw3d-tex openstep-mga-hw3d-tex-client.m -lDriver
 */
#import <stdio.h>
#import <string.h>
#import <mach/mach.h>
#import <driverkit/IODeviceMaster.h>
#import <driverkit/IODevice.h>
#import "OpenStepMGAHW3D.h"

extern int open(const char *, int, ...);
extern int close(int);
extern caddr_t mmap(caddr_t, int, int, int, int, long);

#define O_RDWR 2
#define PROT_READ 0x01
#define PROT_WRITE 0x02
#define MAP_SHARED 0x0001

#define DEV_PATH        "/dev/osmgavram"
#define SUBMIT_PARAM    "OSMGAHW3DSubmit"
#define STATUS_PARAM    "OSMGAHW3DStatus"
#define CMD_MMAP_BASE   0x40000000UL
#define CMD_MMAP_LEN    (64 * 1024)
#define COLOUR_ORG      (4UL * 1024UL * 1024UL)
#define TEX_ORG         (6UL * 1024UL * 1024UL)
#define STRIDE_DW       1024UL
#define DIM             64UL
/* The kernel clips to 120 rows, so the two bands cannot both be 64 tall.
 * The textured one has to be the full texture height; the flat one beside
 * it takes what is left. */
#define FLATH           56UL
#define SENTINEL        0x5A5A5A5AUL

#define DWG_TEX         (0x6UL | (0x7UL << 4))      /* TEXTURE_TRAP | atype I */
#define DWG_FLAT        (0x4UL | (0x7UL << 4))      /* TRAP | atype I */

static caddr_t
mapDevice(int fd, unsigned long offset, int len)
{
    vm_address_t addr = 0;

    if (vm_allocate(task_self(), &addr, (vm_size_t)len, TRUE) != KERN_SUCCESS)
        return (caddr_t)-1;
    if ((int)mmap((caddr_t)addr, len, PROT_READ | PROT_WRITE, MAP_SHARED,
                  fd, (long)offset) == -1)
        return (caddr_t)-1;
    return (caddr_t)addr;
}

static const char *
why(unsigned v)
{
    switch (v) {
    case OSMGA_HW3D_OK:        return "accepted";
    case OSMGA_HW3D_E_TEXORG:  return "texture origin";
    case OSMGA_HW3D_E_TEXSIZE: return "texture size";
    case OSMGA_HW3D_E_DWGCTL:  return "drawing control";
    default:                   return "other";
    }
}

static unsigned
verdict(IODeviceMaster *m, unsigned objNum)
{
    unsigned st[4], n = 4;

    if ([m getIntValues:st forParameter:STATUS_PARAM objectNumber:objNum
            count:&n] != IO_R_SUCCESS)
        return 0xFFFFU;
    return st[0];
}

static void
rect(OSMGAHW3DTri *t, unsigned long y, unsigned long h, unsigned long dwgctl)
{
    memset(t, 0, sizeof *t);
    t->dwgctl = dwgctl;
    t->alphactrl = 0x00000101UL;
    t->y = (long)y;
    t->h = (long)h;
    t->ar0 = (long)h;
    t->ar6 = (long)h;
    t->fxbndry = (DIM << 16) | 0UL;
    t->dr[0] = 200UL << 15;             /* only visible when untextured */
    t->dr[3] = 100UL << 15;
    t->dr[6] =  50UL << 15;
}

static void
texState(OSMGAHW3DBatch *b)
{
    unsigned long step = 1UL << (20UL - 6UL);   /* 64 texels: 0x4000 */

    b->state.texorg = TEX_ORG;
    b->state.texW = DIM;
    b->state.texH = DIM;
    b->state.texPitch = DIM;
    b->state.texFormat = OSMGA_HW3D_TEXFMT_TW32;
    b->state.tmr[0] = step;             /* one texel per pixel in x */
    b->state.tmr[3] = step;             /* and in y */
    b->state.tmr[8] = 1UL << 16;        /* H, which takes no decal */
}

int
main(void)
{
    IODeviceMaster *master;
    IOObjectNumber objNum;
    IOString kind;
    OSMGAHW3DBatch *batch;
    volatile unsigned long *colour, *tex;
    caddr_t cmd, cwin, twin;
    int fd;
    unsigned long row, col, ident = 0UL, drew = 0UL, dirty = 0UL, flat = 0UL;
    unsigned v, fails = 0U;
    IOReturn r;

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) { printf("no Display0\n"); return 1; }
    if ((fd = open(DEV_PATH, O_RDWR)) < 0) { printf("no %s\n", DEV_PATH); return 1; }
    cmd  = mapDevice(fd, CMD_MMAP_BASE, CMD_MMAP_LEN);
    cwin = mapDevice(fd, COLOUR_ORG, (int)((DIM + FLATH) * STRIDE_DW * 4UL));
    twin = mapDevice(fd, TEX_ORG,    (int)(DIM * DIM * 4UL));
    if (cmd == (caddr_t)-1 || cwin == (caddr_t)-1 || twin == (caddr_t)-1) {
        printf("a window will not map\n"); return 1;
    }
    batch  = (OSMGAHW3DBatch *)cmd;
    colour = (volatile unsigned long *)cwin;
    tex    = (volatile unsigned long *)twin;

    printf("M1-2g: texture through the batch path\n");

    /* Each texel carries its own coordinates, so the readback says which
     * texel arrived rather than only that something did. */
    for (row = 0UL; row < DIM; row++)
        for (col = 0UL; col < DIM; col++)
            tex[row * DIM + col] = (col << 16) | (row << 8) | 0x40UL;
    for (row = 0UL; row < DIM + FLATH; row++)
        for (col = 0UL; col < DIM; col++)
            colour[row * STRIDE_DW + col] = SENTINEL;

    /* One textured triangle and one flat one in the same batch: if the
     * texture state leaked into the flat band it would show as texels. */
    memset(batch, 0, sizeof *batch);
    batch->magic = OSMGA_HW3D_MAGIC;
    batch->version = OSMGA_HW3D_VERSION;
    batch->triCount = 2;
    batch->state.dstorg = COLOUR_ORG;
    texState(batch);
    rect(&batch->tri[0], 0UL, DIM, DWG_TEX);
    rect(&batch->tri[1], DIM, FLATH, DWG_FLAT);

    r = [master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                objectNumber:objNum count:0];
    printf("   submit returned %d, verdict %u (%s)\n", (int)r,
           verdict(master, objNum), why(verdict(master, objNum)));
    if (r != IO_R_SUCCESS) return 1;

    for (row = 0UL; row < DIM; row++)
        for (col = 0UL; col < DIM; col++) {
            unsigned long got = colour[row * STRIDE_DW + col];

            if (got != SENTINEL) drew++;
            if (got == tex[row * DIM + col]) ident++;
        }
    for (row = DIM; row < DIM + FLATH; row++)
        for (col = 0UL; col < DIM; col++)
            if (colour[row * STRIDE_DW + col] != SENTINEL) flat++;
    for (row = 0UL; row < DIM; row++)
        for (col = 0UL; col < DIM; col++)
            if (tex[row * DIM + col] != ((col << 16) | (row << 8) | 0x40UL))
                dirty++;

    printf("   textured band: drew %lu, identity %lu of %lu\n",
           drew, ident, DIM * DIM);
    printf("   flat band: drew %lu of %lu; its colour at (0,0) is %06lx "
           "(the interpolators say 0xc86432)\n",
           flat, FLATH * DIM, colour[DIM * STRIDE_DW] & 0xffffffUL);
    printf("   texture region modified: %lu words\n", dirty);
    if (ident != DIM * DIM) fails++;
    if (flat != FLATH * DIM) fails++;
    if ((colour[DIM * STRIDE_DW] & 0xffffffUL) != 0x00c86432UL) fails++;
    if (dirty != 0UL) fails++;

    /* The validator, both ways round. */
    batch->state.texorg = 0UL;
    (void)[master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                  objectNumber:objNum count:0];
    v = verdict(master, objNum);
    printf("   texorg at the visible framebuffer -> verdict %u (%s)\n",
           v, why(v));
    if (v != OSMGA_HW3D_E_TEXORG) fails++;

    batch->tri[0].dwgctl = DWG_FLAT;        /* nothing textured now */
    v = ([master setIntValues:(unsigned *)0 forParameter:SUBMIT_PARAM
                 objectNumber:objNum count:0] == IO_R_SUCCESS)
        ? OSMGA_HW3D_OK : verdict(master, objNum);
    printf("   the same bad texorg with no textured triangle -> verdict %u "
           "(%s)\n", v, why(v));
    if (v != OSMGA_HW3D_OK) fails++;

    if (dirty != 0UL)
        printf("STOP -- the texture region was written to\n");
    else if (fails != 0U)
        printf("FAIL -- %u checks disagreed\n", fails);
    else
        printf("PASS -- every pixel took its own texel through the batch "
               "path, the flat triangle beside it stayed flat, and the "
               "texture origin is checked only when something is "
               "textured\n");
    (void)close(fd);
    return 0;
}
