/* Confirm that a lease remains active after a separate client exits. */

#include <mach/mach.h>
#include <servers/netname.h>
#include <stdio.h>

#include "OpenStepMGAUser.h"
#include "OpenStepMGAProtocol.h"
#include "OpenStepMGAClientPort.h"

#define OPENSTEP_MGA_SERVICE_NAME "openstepmga0"

int
main(void)
{
    kern_return_t kr;
    port_t service;
    port_t clientPort;
    int token;
    int generation;

    service = PORT_NULL;
    clientPort = PORT_NULL;
    token = 0;
    generation = 0;
    kr = netname_look_up(name_server_port, "", OPENSTEP_MGA_SERVICE_NAME,
                         &service);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "lease-busy-probe: lookup=%d\n", (int)kr);
        return 1;
    }

    kr = OSMGAAllocateClientPort(&clientPort);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "lease-busy-probe: client-port=%d\n", (int)kr);
        return 2;
    }

    kr = OSMGA_acquire(service, OSMGA_PROTOCOL_VERSION, clientPort,
                       &token, &generation);
    printf("OPENSTEP_MGA_LEASE_BUSY_PROBE result=%d token=%d generation=%d\n",
           (int)kr, token, generation);
    if (kr == KERN_RESOURCE_SHORTAGE) {
        OSMGADeallocateClientPort(clientPort);
        return 0;
    }

    /* Unexpected success must not leave a lease behind while reporting failure. */
    if (kr == KERN_SUCCESS) {
        (void)OSMGA_release(service, token, generation);
    }
    OSMGADeallocateClientPort(clientPort);
    return 3;
}
