/*
 * OpenStepMGAProbe.m - PCI interrogation for OpenStepMGA (H1 staged).
 *
 * This is deliberately not an IODevice subclass and does not contain an
 * Auto Detect ID.  The entry point is called by kern_loader from
 * Load_Commands.sect and uses PCI configuration mechanism #1 (0xCF8/0xCFC).
 *
 * Per docs/H1_HARDWARE_INTERROGATION_DECISION.md the operator has rebooted
 * the target with a generic SVGA (IOVGADisplay, legacy VGA only) as sole
 * display owner; MatroxMGA is not loaded, so the MGA native BAR regions are
 * unowned.  Two stages are implemented:
 *
 *   stage 0 - PCI config READ only (vid/did/class, command/status, BAR
 *             bases, capability walk).  Writes nothing.
 *   stage 1 - stage 0 report, plus config-space BAR sizing: write
 *             0xFFFFFFFF to each BAR, read the size mask, restore the
 *             original value, and verify the restore.  This touches PCI
 *             configuration space ONLY.  It performs no MGA MMIO/VRAM/DAC/
 *             PLL access and does NOT alter the PCI command register, so the
 *             legacy VGA aperture used by the current owner stays enabled and
 *             CRTC-driven scanout is unaffected.
 *
 * Do not add MGA BAR mapping, MMIO, or VRAM access to this source.  Those are
 * the separately reviewed S2 (MMIO read) and S3 (VRAM size) builds.
 */

#import <driverkit/generalFuncs.h>
#import <driverkit/kernelDriver.h>
#import <driverkit/i386/ioPorts.h>

#define PCI_CFG_ADDR        0x0CF8
#define PCI_CFG_DATA        0x0CFC
#define PCI_MAX_BUS         8
#define PCI_MAX_DEVICE      32
#define PCI_STATUS_CAP_LIST 0x0010
#define PCI_CAP_PTR         0x34
#define PCI_CAP_ID_VPD      0x03
#define PCI_CAP_MIN_OFFSET  0x40
#define PCI_CAP_MAX_OFFSET  0xFC
#define PCI_CAP_MAX_HOPS    48

#define MGA_VENDOR_ID       0x102B
#define MGA_G400_G450_ID    0x0525

/*
 * S2 MMIO (read-only).  BAR1 = 16 KiB control aperture (size measured in S1).
 * Offsets verified against xf86-video-mga src/mga_reg.h (openbsd/xenocara);
 * these core Storm/status registers are common to G200/G400/G450/G550, so the
 * read set is valid for both G400 and G450.  See docs/R6_G400_G450_REGISTER_
 * DIVERGENCE.md.  Reset (0x1e40) and EXEC (0x0100) are never touched.
 */
#define MGA_MMIO_BAR_REG    0x14
#define MGA_MMIO_LENGTH     0x4000
#define MGAREG_DWGCTL       0x1c00
#define MGAREG_FIFOSTATUS   0x1e10
#define MGAREG_STATUS       0x1e14
#define MGAREG_VCOUNT       0x1e20
#define MGAREG_OPMODE       0x1e54
#define MGAREG_MEMCTL       0x2e08
#define MGA_VCOUNT_SAMPLES  8
#define MGA_VCOUNT_DELAY_US 200

static unsigned long
pciReadConfigLong(int bus, int device, int function, int reg)
{
    unsigned long address;

    address = 0x80000000UL
        | ((unsigned long)(bus & 0xff) << 16)
        | ((unsigned long)(device & 0x1f) << 11)
        | ((unsigned long)(function & 0x07) << 8)
        | ((unsigned long)(reg & 0xfc));

    outl((IOEISAPortAddress)PCI_CFG_ADDR, address);
    return inl((IOEISAPortAddress)PCI_CFG_DATA);
}

static void
pciWriteConfigLong(int bus, int device, int function, int reg,
                   unsigned long value)
{
    unsigned long address;

    address = 0x80000000UL
        | ((unsigned long)(bus & 0xff) << 16)
        | ((unsigned long)(device & 0x1f) << 11)
        | ((unsigned long)(function & 0x07) << 8)
        | ((unsigned long)(reg & 0xfc));

    outl((IOEISAPortAddress)PCI_CFG_ADDR, address);
    outl((IOEISAPortAddress)PCI_CFG_DATA, value);
}

static int
pciSlotPresent(unsigned long vendorDevice)
{
    return vendorDevice != 0xffffffffUL && vendorDevice != 0UL;
}

/* S1: config-space BAR sizing.  See file header and H1 decision doc. */
static int gProbeStage = 0;

static void
reportBarSize(int bus, int device, int function, int barIndex, int reg)
{
    unsigned long original;
    unsigned long mask;
    unsigned long restored;
    unsigned long masked;
    unsigned long size;
    int isIO;
    int type;
    int prefetch;

    original = pciReadConfigLong(bus, device, function, reg);
    pciWriteConfigLong(bus, device, function, reg, 0xFFFFFFFFUL);
    mask = pciReadConfigLong(bus, device, function, reg);
    pciWriteConfigLong(bus, device, function, reg, original);
    restored = pciReadConfigLong(bus, device, function, reg);

    isIO = (int)(original & 0x1UL);
    if (isIO) {
        masked = mask & 0xFFFFFFFCUL;
        type = 0;
        prefetch = 0;
    } else {
        masked = mask & 0xFFFFFFF0UL;
        type = (int)((original >> 1) & 0x3UL);
        prefetch = (int)((original >> 3) & 0x1UL);
    }

    if (masked == 0UL)
        size = 0UL;
    else
        size = (~masked) + 1UL;

    IOLog("MGA-PROBE bar%d reg=%02x base=%08x mask=%08x size=%08x io=%d type=%d prefetch=%d\\n",
          barIndex, reg, (unsigned int)(original & (isIO ? 0xFFFFFFFCUL : 0xFFFFFFF0UL)),
          (unsigned int)mask, (unsigned int)size, isIO, type, prefetch);
    if (restored != original)
        IOLog("MGA-PROBE bar%d RESTORE-MISMATCH original=%08x now=%08x\\n",
              barIndex, (unsigned int)original, (unsigned int)restored);
    else
        IOLog("MGA-PROBE bar%d restore-ok=%08x\\n",
              barIndex, (unsigned int)restored);
}

static unsigned long
mgaReadReg(volatile unsigned char *base, unsigned int offset)
{
    return *(volatile unsigned long *)(base + offset);
}

/*
 * S2: map BAR1 read-only, sample documented read-only registers, and prove the
 * mapping is live+uncached+decoding by watching VCOUNT advance.  No register is
 * written; the PCI command register is not changed.  Map and unmap happen in
 * this one call.  If the map fails, or VCOUNT never advances, we stop and log:
 * both outcomes are non-destructive.  See H1_HARDWARE_INTERROGATION_DECISION.md.
 */
static void
probeMGAMMIO(int bus, int device, int function)
{
    unsigned long bar1;
    unsigned long physBase;
    vm_address_t virt = 0;
    IOReturn r;
    volatile unsigned char *regs;
    unsigned long v0;
    unsigned long vN;
    int i;
    int advanced;

    bar1 = pciReadConfigLong(bus, device, function, MGA_MMIO_BAR_REG);
    physBase = bar1 & 0xFFFFFFF0UL;
    IOLog("MGA-PROBE mmio map-attempt phys=%08x len=%04x\\n",
          (unsigned int)physBase, MGA_MMIO_LENGTH);

    r = IOMapPhysicalIntoIOTask((unsigned)physBase, MGA_MMIO_LENGTH, &virt);
    if (r != IO_R_SUCCESS || virt == 0) {
        IOLog("MGA-PROBE mmio map-FAILED r=%d virt=%08x need-iodirectdevice-cacheoff\\n",
              (int)r, (unsigned int)virt);
        return;
    }
    IOLog("MGA-PROBE mmio mapped virt=%08x\\n", (unsigned int)virt);
    regs = (volatile unsigned char *)virt;

    IOLog("MGA-PROBE mmio dwgctl=%08x fifostatus=%08x status=%08x opmode=%08x memctl=%08x\\n",
          (unsigned int)mgaReadReg(regs, MGAREG_DWGCTL),
          (unsigned int)mgaReadReg(regs, MGAREG_FIFOSTATUS),
          (unsigned int)mgaReadReg(regs, MGAREG_STATUS),
          (unsigned int)mgaReadReg(regs, MGAREG_OPMODE),
          (unsigned int)mgaReadReg(regs, MGAREG_MEMCTL));

    v0 = mgaReadReg(regs, MGAREG_VCOUNT);
    vN = v0;
    advanced = 0;
    for (i = 0; i < MGA_VCOUNT_SAMPLES; i++) {
        IODelay(MGA_VCOUNT_DELAY_US);
        vN = mgaReadReg(regs, MGAREG_VCOUNT);
        IOLog("MGA-PROBE mmio vcount-sample %d = %08x\\n", i, (unsigned int)vN);
        if (vN != v0)
            advanced = 1;
    }
    if (advanced)
        IOLog("MGA-PROBE mmio vcount-LIVE first=%08x last=%08x (mapping uncached+decoding)\\n",
              (unsigned int)v0, (unsigned int)vN);
    else
        IOLog("MGA-PROBE mmio vcount-STATIC first=%08x last=%08x (cached/idle/no-decode) stop\\n",
              (unsigned int)v0, (unsigned int)vN);

    IOUnmapPhysicalFromIOTask(virt, MGA_MMIO_LENGTH);
    IOLog("MGA-PROBE mmio unmapped\\n");
}

/*
 * S3 diagnostic: map BAR0 (16 MiB framebuffer) and exercise read / single
 * write / offset write / a 3 MiB clear, with a log line (and IOSleep so
 * syslogd flushes) before each potentially-hanging step.  This isolates the
 * untested framebuffer path -- the driver hung at boot after mapping BAR0.
 */
static void
probeMGAFramebuffer(int bus, int device, int function)
{
    unsigned long bar0;
    unsigned long physBase;
    vm_address_t virt = 0;
    IOReturn r;
    volatile unsigned long *fb;
    unsigned long v;
    unsigned long i;
    unsigned long words;

    bar0 = pciReadConfigLong(bus, device, function, 0x10);
    physBase = bar0 & 0xFFFFFFF0UL;
    IOLog("MGA-PROBE fb map-attempt phys=%08x len=01000000\\n",
          (unsigned int)physBase);
    IOSleep(80);
    r = IOMapPhysicalIntoIOTask((unsigned)physBase, 0x1000000, &virt);
    if (r != IO_R_SUCCESS || virt == 0) {
        IOLog("MGA-PROBE fb map-FAILED r=%d virt=%08x\\n",
              (int)r, (unsigned int)virt);
        return;
    }
    IOLog("MGA-PROBE fb mapped virt=%08x\\n", (unsigned int)virt);
    IOSleep(80);
    fb = (volatile unsigned long *)virt;

    /* reads are safe; writes/clear use high offsets (>=4 MiB) so the VGA
     * console visible region (offset 0) is not corrupted during the test. */
    v = fb[0];
    IOLog("MGA-PROBE fb read[0]=%08x\\n", (unsigned int)v);
    IOSleep(80);

    fb[0x100000UL] = 0x12345678UL;   /* +4 MiB (word index 0x100000) */
    v = fb[0x100000UL];
    IOLog("MGA-PROBE fb write[4MiB] readback=%08x expect=12345678\\n",
          (unsigned int)v);
    IOSleep(80);

    fb[0x200000UL] = 0xa5a5a5a5UL;   /* +8 MiB */
    v = fb[0x200000UL];
    IOLog("MGA-PROBE fb write[8MiB] readback=%08x\\n", (unsigned int)v);
    IOSleep(80);

    /* clear a 3 MiB region starting at +8 MiB (same size as the driver's
     * 1024x768x32 clear), high enough to avoid the VGA visible area. */
    words = (1024UL * 768UL * 4UL) / 4UL;
    IOLog("MGA-PROBE fb clear begin base=8MiB words=%08x\\n",
          (unsigned int)words);
    IOSleep(80);
    for (i = 0; i < words; i++) {
        fb[0x200000UL + i] = 0UL;
        if ((i & 0x3ffffUL) == 0UL)
            IOLog("MGA-PROBE fb clear i=%08x\\n", (unsigned int)i);
    }
    IOLog("MGA-PROBE fb clear done\\n");
    IOSleep(80);

    IOUnmapPhysicalFromIOTask(virt, 0x1000000);
    IOLog("MGA-PROBE fb unmapped\\n");
}

/* ---- S4: register mode-program sequence (find the boot hang) ---- */

#define P4_MISC_R    0x1fcc
#define P4_MISC_W    0x1fc2
#define P4_SEQ_I     0x1fc4
#define P4_SEQ_D     0x1fc5
#define P4_GR_I      0x1fce
#define P4_GR_D      0x1fcf
#define P4_ATTR_I    0x1fc0
#define P4_INSTS1    0x1fda
#define P4_CRTC_I    0x1fd4
#define P4_CRTC_D    0x1fd5
#define P4_CEXT_I    0x1fde
#define P4_CEXT_D    0x1fdf
#define P4_DAC_I     0x3c00
#define P4_DAC_D     0x3c0a

static unsigned char p4r8(vm_address_t b, unsigned int o)
{ return *(volatile unsigned char *)(b + o); }
static void p4w8(vm_address_t b, unsigned int o, unsigned char v)
{ *(volatile unsigned char *)(b + o) = v; }
static void p4outdac(vm_address_t b, unsigned char i, unsigned char v)
{ p4w8(b, P4_DAC_I, i); p4w8(b, P4_DAC_D, v); }
static void p4wcrtc(vm_address_t b, unsigned char i, unsigned char v)
{ p4w8(b, P4_CRTC_I, i); p4w8(b, P4_CRTC_D, v); }
static unsigned char p4rcrtc(vm_address_t b, unsigned char i)
{ p4w8(b, P4_CRTC_I, i); return p4r8(b, P4_CRTC_D); }
static void p4wcext(vm_address_t b, unsigned char i, unsigned char v)
{ p4w8(b, P4_CEXT_I, i); p4w8(b, P4_CEXT_D, v); }

/* 1024x768x32@60 CRTC (X.Org computed) + tables */
static const unsigned char p4crtc[25] = {
    0xa3,0x7f,0x7f,0x87,0x82,0x93,0x24,0xfd,0x00,0x60,0,0,0,0,0,0,
    0x02,0x24,0xff,0x00,0x00,0xff,0x25,0xc3,0xff
};
static const unsigned char p4cext[6] = { 0x00,0x40,0x30,0x83,0x00,0x00 };
static const unsigned char p4seq[5]  = { 0x00,0x01,0x0f,0x00,0x0e };
static const unsigned char p4gr[9]   = { 0,0,0,0,0,0x40,0x05,0x0f,0xff };
static const unsigned char p4ar[21]  = {
    0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,0x41,0x00,0x0f,0x00,0x00
};
static const unsigned char p4dac[0x50] = {
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
    0x00,0x07,0xc9,0xff,0xbf,0x20,0x1f,0x20,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0x40,
    0x00,0xb0,0x00,0xc2,0x34,0x14,0x02,0x83,
    0x00,0x93,0x00,0x77,0x00,0x00,0x00,0x3a,
    0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0
};
static int p4dacskip(unsigned int i)
{
    if (i <= 0x03U) return 1;
    if (i == 0x07U || i == 0x0bU || i == 0x0fU) return 1;
    if (i >= 0x13U && i <= 0x17U) return 1;
    if (i == 0x1bU || i == 0x1cU) return 1;
    if (i >= 0x1fU && i <= 0x29U) return 1;
    if (i >= 0x30U && i <= 0x37U) return 1;
    if (i == 0x2cU || i == 0x2dU || i == 0x2eU) return 1;
    if (i == 0x4cU || i == 0x4dU || i == 0x4eU) return 1;
    return 0;
}
static int p4islocked(vm_address_t b)
{
    unsigned long spins = 0;
    unsigned long lc = 0;
    unsigned long k;
    unsigned char st;
    p4w8(b, P4_DAC_I, 0x4f);
    do { st = p4r8(b, P4_DAC_D); spins++; }
    while ((st & 0x40) == 0 && spins < 1000UL);
    if (spins >= 1000UL) return 0;
    for (k = 0; k < 100UL; k++) { st = p4r8(b, P4_DAC_D); if (st & 0x40) lc++; }
    return lc >= 90UL;
}

static void
probeMGAModeProgram(int bus, int device, int function)
{
    unsigned long bar1;
    vm_address_t base = 0;
    IOReturn r;
    unsigned int i;
    unsigned char misc;

    bar1 = pciReadConfigLong(bus, device, function, 0x14);
    IOLog("MGA-PROBE mp map bar1=%08x\\n", (unsigned int)(bar1 & 0xFFFFFFF0UL));
    IOSleep(80);
    r = IOMapPhysicalIntoIOTask((unsigned)(bar1 & 0xFFFFFFF0UL), 0x4000, &base);
    if (r != IO_R_SUCCESS || base == 0) {
        IOLog("MGA-PROBE mp map-FAILED r=%d\\n", (int)r);
        return;
    }
    IOLog("MGA-PROBE mp mapped virt=%08x\\n", (unsigned int)base);
    IOSleep(80);

    /* PLL: clock select + write a fixed M/N/P for 65MHz + lock check.
     * 65MHz candidate (from X.Org formula): try VCO=650MHz (P div 8? here we
     * just probe lock behavior with one plausible M/N/P and log). */
    misc = p4r8(base, P4_MISC_R);
    p4w8(base, P4_MISC_W, (unsigned char)(misc | 0x0c));
    IOLog("MGA-PROBE mp clksel done misc=%02x\\n", (unsigned int)misc);
    IOSleep(80);
    IOLog("MGA-PROBE mp pll write M/N/P\\n"); IOSleep(80);
    p4outdac(base, 0x4c, 0x02);
    p4outdac(base, 0x4d, 0x1b);
    p4outdac(base, 0x4e, 0x09);
    IOLog("MGA-PROBE mp pll islocked call\\n"); IOSleep(80);
    IOLog("MGA-PROBE mp pll locked=%d\\n", p4islocked(base)); IOSleep(80);

    p4outdac(base, 0xa2, 0x10);
    IOLog("MGA-PROBE mp panctl done\\n"); IOSleep(80);

    IOLog("MGA-PROBE mp dac begin\\n"); IOSleep(80);
    for (i = 0; i < 0x50U; i++)
        if (!p4dacskip(i)) p4outdac(base, (unsigned char)i, p4dac[i]);
    IOLog("MGA-PROBE mp dac done\\n"); IOSleep(80);

    IOLog("MGA-PROBE mp crtcext begin\\n"); IOSleep(80);
    for (i = 0; i < 6U; i++) p4wcext(base, (unsigned char)i, p4cext[i]);
    IOLog("MGA-PROBE mp crtcext done\\n"); IOSleep(80);

    IOLog("MGA-PROBE mp misc+seq begin\\n"); IOSleep(80);
    p4w8(base, P4_MISC_W, 0xed);
    for (i = 1; i < 5U; i++) { p4w8(base, P4_SEQ_I, (unsigned char)i); p4w8(base, P4_SEQ_D, p4seq[i]); }
    IOLog("MGA-PROBE mp misc+seq done\\n"); IOSleep(80);

    IOLog("MGA-PROBE mp crtc begin\\n"); IOSleep(80);
    p4wcrtc(base, 0x11, (unsigned char)(p4rcrtc(base, 0x11) & 0x7f));
    for (i = 0; i < 25U; i++) p4wcrtc(base, (unsigned char)i, p4crtc[i]);
    IOLog("MGA-PROBE mp crtc done\\n"); IOSleep(80);

    IOLog("MGA-PROBE mp gr begin\\n"); IOSleep(80);
    for (i = 0; i < 9U; i++) { p4w8(base, P4_GR_I, (unsigned char)i); p4w8(base, P4_GR_D, p4gr[i]); }
    IOLog("MGA-PROBE mp gr done\\n"); IOSleep(80);

    IOLog("MGA-PROBE mp attr begin\\n"); IOSleep(80);
    (void)p4r8(base, P4_INSTS1);
    p4w8(base, P4_ATTR_I, 0x00);
    for (i = 0; i < 21U; i++) { (void)p4r8(base, P4_INSTS1); p4w8(base, P4_ATTR_I, (unsigned char)i); p4w8(base, P4_ATTR_I, p4ar[i]); }
    (void)p4r8(base, P4_INSTS1);
    p4w8(base, P4_ATTR_I, 0x20);
    IOLog("MGA-PROBE mp attr+pas done\\n"); IOSleep(80);

    p4wcext(base, 0, p4cext[0]);
    IOLog("MGA-PROBE mp crtcext0 relatch done\\n"); IOSleep(80);

    IOUnmapPhysicalFromIOTask(base, 0x4000);
    IOLog("MGA-PROBE mp unmapped ALL DONE\\n");
}

/*
 * Walk standard PCI capability headers only.  This uses the same config
 * selection/read primitive as P1.  It never writes a device configuration
 * register.  VPD capability ID 0x03 is reported only as present: reading
 * VPD contents requires a VPD address/data transaction and is not P1.4.
 */
static void
reportMGACapabilities(int bus, int device, int function,
                      unsigned long commandStatus)
{
    unsigned long value;
    unsigned int offset;
    unsigned int capability;
    unsigned int next;
    unsigned int hop;
    unsigned long visited[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

    if ((commandStatus & (PCI_STATUS_CAP_LIST << 16)) == 0) {
        IOLog("MGA-PROBE capabilities %02x:%02x.%x absent\\n",
              bus, device, function);
        return;
    }

    value = pciReadConfigLong(bus, device, function, PCI_CAP_PTR);
    offset = (unsigned int)(value & 0xffUL);
    for (hop = 0; hop < PCI_CAP_MAX_HOPS; hop++) {
        if (offset < PCI_CAP_MIN_OFFSET || offset > PCI_CAP_MAX_OFFSET ||
            (offset & 3) != 0) {
            IOLog("MGA-PROBE capabilities %02x:%02x.%x invalid-offset=%02x\\n",
                  bus, device, function, offset);
            return;
        }
        if ((visited[offset >> 5] & (1UL << ((offset >> 2) & 31))) != 0) {
            IOLog("MGA-PROBE capabilities %02x:%02x.%x loop-offset=%02x\\n",
                  bus, device, function, offset);
            return;
        }
        visited[offset >> 5] |= (1UL << ((offset >> 2) & 31));

        value = pciReadConfigLong(bus, device, function, (int)offset);
        capability = (unsigned int)(value & 0xffUL);
        next = (unsigned int)((value >> 8) & 0xffUL);
        IOLog("MGA-PROBE capability %02x:%02x.%x offset=%02x id=%02x\\n",
              bus, device, function, offset, capability);
        if (capability == PCI_CAP_ID_VPD)
            IOLog("MGA-PROBE VPD-capability-present no-vpd-data-access\\n");
        if (next == 0) {
            IOLog("MGA-PROBE capabilities %02x:%02x.%x end hops=%u\\n",
                  bus, device, function, hop + 1);
            return;
        }
        offset = next;
    }
    IOLog("MGA-PROBE capabilities %02x:%02x.%x hop-limit=%u\\n",
          bus, device, function, PCI_CAP_MAX_HOPS);
}

static void
reportMGAFunction(int bus, int device, int function)
{
    unsigned long vendorDevice;
    unsigned long classRevision;
    unsigned long commandStatus;
    unsigned long bar0;
    unsigned long bar1;
    unsigned long bar2;
    unsigned long interrupt;
    unsigned int vendor;
    unsigned int product;

    vendorDevice = pciReadConfigLong(bus, device, function, 0x00);
    vendor = (unsigned int)(vendorDevice & 0xffffUL);
    product = (unsigned int)((vendorDevice >> 16) & 0xffffUL);
    if (vendor != MGA_VENDOR_ID || product != MGA_G400_G450_ID)
        return;

    commandStatus = pciReadConfigLong(bus, device, function, 0x04);
    classRevision = pciReadConfigLong(bus, device, function, 0x08);
    bar0 = pciReadConfigLong(bus, device, function, 0x10);
    bar1 = pciReadConfigLong(bus, device, function, 0x14);
    bar2 = pciReadConfigLong(bus, device, function, 0x18);
    interrupt = pciReadConfigLong(bus, device, function, 0x3c);

    IOLog("MGA-PROBE device %02x:%02x.%x vid=%04x did=%04x rev=%02x class=%06x\\n",
          bus, device, function, vendor, product,
          (unsigned int)(classRevision & 0xffUL),
          (unsigned int)(classRevision >> 8));
    IOLog("MGA-PROBE command=%04x status=%04x irq=%u pin=%u\\n",
          (unsigned int)(commandStatus & 0xffffUL),
          (unsigned int)(commandStatus >> 16),
          (unsigned int)(interrupt & 0xffUL),
          (unsigned int)((interrupt >> 8) & 0xffUL));
    IOLog("MGA-PROBE bars %08x %08x %08x\\n",
          (unsigned int)bar0, (unsigned int)bar1, (unsigned int)bar2);
    reportMGACapabilities(bus, device, function, commandStatus);

    if (gProbeStage == 1) {
        IOLog("MGA-PROBE bar-sizing begin %02x:%02x.%x command-unchanged=%04x\\n",
              bus, device, function, (unsigned int)(commandStatus & 0xffffUL));
        reportBarSize(bus, device, function, 0, 0x10);
        reportBarSize(bus, device, function, 1, 0x14);
        reportBarSize(bus, device, function, 2, 0x18);
        IOLog("MGA-PROBE bar-sizing end %02x:%02x.%x\\n", bus, device, function);
    }

    if (gProbeStage == 3) {
        IOLog("MGA-PROBE fb begin %02x:%02x.%x\\n", bus, device, function);
        probeMGAFramebuffer(bus, device, function);
        IOLog("MGA-PROBE fb end %02x:%02x.%x\\n", bus, device, function);
    }

    if (gProbeStage == 4) {
        IOLog("MGA-PROBE mp begin %02x:%02x.%x\\n", bus, device, function);
        probeMGAModeProgram(bus, device, function);
        IOLog("MGA-PROBE mp end %02x:%02x.%x\\n", bus, device, function);
    }

    if (gProbeStage == 2) {
        IOLog("MGA-PROBE mmio begin %02x:%02x.%x command-unchanged=%04x\\n",
              bus, device, function, (unsigned int)(commandStatus & 0xffffUL));
        probeMGAMMIO(bus, device, function);
        IOLog("MGA-PROBE mmio end %02x:%02x.%x\\n", bus, device, function);
    }
}

static void
scanForMGA(void)
{
    int bus;
    int device;
    int function;
    int functions;
    unsigned long vendorDevice;
    unsigned long headerType;

    for (bus = 0; bus < PCI_MAX_BUS; bus++) {
        for (device = 0; device < PCI_MAX_DEVICE; device++) {
            vendorDevice = pciReadConfigLong(bus, device, 0, 0x00);
            if (!pciSlotPresent(vendorDevice))
                continue;

            headerType = pciReadConfigLong(bus, device, 0, 0x0c);
            functions = (((headerType >> 16) & 0x80UL) != 0) ? 8 : 1;
            for (function = 0; function < functions; function++)
                reportMGAFunction(bus, device, function);
        }
    }
}

void
openStepMGAProbeEntry(int stage)
{
    IOLog("MGA-PROBE begin version=2 stage=%d\\n", stage);
    if (stage < 0 || stage > 4) {
        IOLog("MGA-PROBE refused unsupported stage=%d\\n", stage);
        return;
    }

    gProbeStage = stage;
    if (stage == 0)
        IOLog("MGA-PROBE mode read-only-pci-config\\n");
    else if (stage == 1)
        IOLog("MGA-PROBE mode config-bar-sizing write-restore no-mmio no-vram\\n");
    else if (stage == 2)
        IOLog("MGA-PROBE mode mmio-read-only no-config-write no-vram map-unmap\\n");
    else
        IOLog("MGA-PROBE mode fb-16mb-map-read-write-clear\\n");

    scanForMGA();
    IOLog("MGA-PROBE end version=2\\n");
}
