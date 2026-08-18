/* Hold one P2.2 lease long enough for a separate busy-client probe. */

#include <mach/mach.h>
#include <servers/netname.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "OpenStepMGAUser.h"
#include "OpenStepMGAProtocol.h"
#include "OpenStepMGAClientPort.h"

#define OPENSTEP_MGA_SERVICE_NAME "openstepmga0"

/* OPENSTEP's installed unistd.h omits this libc declaration. */
extern unsigned sleep(unsigned seconds);

int
main(int argc, char **argv)
{
    kern_return_t kr;
    port_t service;
    port_t clientPort;
    int token;
    int generation;
    int seconds;

    seconds = 3;
    if (argc == 2) {
        seconds = atoi(argv[1]);
    }
    if (seconds < 1 || seconds > 10) {
        fprintf(stderr, "lease-hold: seconds must be 1..10\n");
        return 1;
    }

    service = PORT_NULL;
    clientPort = PORT_NULL;
    token = 0;
    generation = 0;
    kr = netname_look_up(name_server_port, "", OPENSTEP_MGA_SERVICE_NAME,
                         &service);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "lease-hold: lookup=%d\n", (int)kr);
        return 2;
    }
    kr = OSMGAAllocateClientPort(&clientPort);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "lease-hold: client-port=%d\n", (int)kr);
        return 3;
    }
    kr = OSMGA_acquire(service, OSMGA_PROTOCOL_VERSION, clientPort,
                       &token, &generation);
    printf("OPENSTEP_MGA_LEASE_HOLD_ACQUIRE result=%d token=%d generation=%d\n",
           (int)kr, token, generation);
    if (kr != KERN_SUCCESS) {
        OSMGADeallocateClientPort(clientPort);
        return 4;
    }

    sleep(seconds);
    kr = OSMGA_release(service, token, generation);
    printf("OPENSTEP_MGA_LEASE_HOLD_RELEASE result=%d\n", (int)kr);
    OSMGADeallocateClientPort(clientPort);
    return kr == KERN_SUCCESS ? 0 : 5;
}
