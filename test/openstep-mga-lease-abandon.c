/* Acquire one P2.1 software lease and deliberately exit without release. */

#include <mach/mach.h>
#include <servers/netname.h>
#include <stdio.h>

#include "OpenStepMGAUser.h"
#include "OpenStepMGAProtocol.h"
#define OSMGA_CLIENT_PORT_NO_DEALLOCATE 1
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
        fprintf(stderr, "lease-abandon: lookup=%d\n", (int)kr);
        return 1;
    }

    kr = OSMGAAllocateClientPort(&clientPort);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "lease-abandon: client-port=%d\n", (int)kr);
        return 2;
    }

    kr = OSMGA_acquire(service, OSMGA_PROTOCOL_VERSION, clientPort,
                       &token, &generation);
    printf("OPENSTEP_MGA_LEASE_ABANDON result=%d token=%d generation=%d\n",
           (int)kr, token, generation);
    /* Deliberately retain clientPort until process exit to trigger death. */
    return kr == KERN_SUCCESS ? 0 : 3;
}
