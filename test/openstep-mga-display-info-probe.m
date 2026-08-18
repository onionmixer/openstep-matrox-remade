/*
 * P1.2 read-only DriverKit display information probe.
 *
 * This program uses documented IODeviceMaster getter RPCs against the
 * already-loaded display driver.  It does not map a BAR, request a device,
 * set a parameter, or obtain the framebuffer pointer carried by
 * IO_GET_DISPLAY_INFO.
 */

#import <driverkit/IODeviceMaster.h>
#import <driverkit/IODevice.h>
#import <driverkit/displayDefs.h>
#import <driverkit/return.h>

#include <stdio.h>
#include <string.h>

static IOReturn
getDisplayInt(IODeviceMaster *master, IOObjectNumber objectNumber,
              const char *parameterText, unsigned *value, unsigned *count)
{
    IOParameterName parameter;

    strcpy(parameter, parameterText);
    return [master getIntValues:value forParameter:parameter
                    objectNumber:objectNumber count:count];
}

int
main(void)
{
    static const char *candidates[] = {
        "MatroxMGA0", "MatroxMGA", "Display0", "Display"
    };
    IODeviceMaster *master;
    IOObjectNumber objectNumber;
    IOString deviceName;
    IOString deviceKind;
    IOReturn result;
    unsigned value;
    unsigned count;
    int i;
    int found;

    master = [IODeviceMaster new];
    if (master == nil) {
        fprintf(stderr, "display-info-probe: IODeviceMaster unavailable\n");
        return 1;
    }

    found = 0;
    objectNumber = 0;
    for (i = 0; i < (int)(sizeof(candidates) / sizeof(candidates[0])); i++) {
        strcpy(deviceName, candidates[i]);
        result = [master lookUpByDeviceName:deviceName
                               objectNumber:&objectNumber
                                 deviceKind:&deviceKind];
        if (result == IO_R_SUCCESS) {
            printf("OPENSTEP_MGA_DISPLAY_LOOKUP candidate=%s result=%d object=%u kind=%s\n",
                   candidates[i], (int)result, (unsigned)objectNumber, deviceKind);
        } else {
            printf("OPENSTEP_MGA_DISPLAY_LOOKUP candidate=%s result=%d\n",
                   candidates[i], (int)result);
        }
        if (result == IO_R_SUCCESS) {
            found = 1;
            break;
        }
    }
    if (!found) {
        [master free];
        return 2;
    }

    value = 0;
    count = 1;
    result = getDisplayInt(master, objectNumber, IO_GET_DISPLAY_MEMORY,
                           &value, &count);
    printf("OPENSTEP_MGA_DISPLAY_MEMORY result=%d count=%u bytes=%u\n",
           (int)result, count, value);
    if (result != IO_R_SUCCESS || count != 1) {
        [master free];
        return 3;
    }

    value = 0;
    count = 1;
    result = getDisplayInt(master, objectNumber, IO_GET_RAMDAC_SPEED,
                           &value, &count);
    printf("OPENSTEP_MGA_DISPLAY_RAMDAC result=%d count=%u hz=%u\n",
           (int)result, count, value);
    if (result != IO_R_SUCCESS || count != 1) {
        [master free];
        return 4;
    }

    value = 0;
    count = 1;
    result = getDisplayInt(master, objectNumber, IO_GET_CURRENT_DISPLAY_MODE,
                           &value, &count);
    printf("OPENSTEP_MGA_DISPLAY_CURRENT_MODE result=%d count=%u index=%u\n",
           (int)result, count, value);
    if (result != IO_R_SUCCESS || count != 1) {
        [master free];
        return 5;
    }

    value = 0;
    count = 1;
    result = getDisplayInt(master, objectNumber, IO_GET_DISPLAY_MODE_NUM,
                           &value, &count);
    printf("OPENSTEP_MGA_DISPLAY_MODE_COUNT result=%d count=%u modes=%u\n",
           (int)result, count, value);
    if (result != IO_R_SUCCESS || count != 1 || value == 0) {
        [master free];
        return 6;
    }

    [master free];
    return 0;
}
