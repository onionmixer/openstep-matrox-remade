/*
 * Turn the VGA restore on or off on a running machine.
 *
 *   vgarestore        say what the driver reports without changing it
 *   vgarestore 1      revertToVGAMode puts the console's card back
 *   vgarestore 0      it does not (the default)
 *
 * The driver logs what it decided, including whether the console was in a
 * graphics or a text mode, because a text console needs character generator
 * planes this snapshot does not hold.
 */
#import <driverkit/IODeviceMaster.h>
#import <driverkit/return.h>
#import <stdio.h>
#import <stdlib.h>

#define RESTORE_PARAM "OSMGAVgaRestore"

int
main(int argc, char **argv)
{
    IODeviceMaster *master;
    IOObjectNumber objNum;
    IOString kind;
    unsigned v[1];
    IOReturn r;

    master = [IODeviceMaster new];
    if ([master lookUpByDeviceName:"Display0" objectNumber:&objNum
            deviceKind:&kind] != IO_R_SUCCESS) {
        printf("Display0 not found\n");
        return 1;
    }
    if (argc < 2) {
        printf("usage: vgarestore 0|1   (the driver logs what it decided)\n");
        return 0;
    }
    v[0] = (unsigned)atoi(argv[1]);
    r = [master setIntValues:v forParameter:RESTORE_PARAM
                objectNumber:objNum count:1];
    if (r != IO_R_SUCCESS) {
        printf("refused (%d) -- 0 or 1, and the driver must be new enough\n",
               (int)r);
        return 2;
    }
    printf("vga restore set to %u; the driver logged the rest\n", v[0]);
    return 0;
}
