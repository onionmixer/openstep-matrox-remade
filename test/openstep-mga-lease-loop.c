/*
 * P2.1 repeatable software-lease test.
 *
 * No request in this program contains MGA hardware state.  It only verifies
 * that sequential Acquire/Release traffic leaves the service READY.
 */

#include <mach/mach.h>
#include <servers/netname.h>
#include <stdio.h>
#include <stdlib.h>

#include "OpenStepMGAUser.h"
#include "OpenStepMGAProtocol.h"
#include "OpenStepMGAClientPort.h"

#define OPENSTEP_MGA_SERVICE_NAME "openstepmga0"
#define DEFAULT_ITERATIONS 1000
#define MAX_ITERATIONS 10000

int
main(int argc, char **argv)
{
    kern_return_t kr;
    port_t service;
    port_t clientPort;
    int iterations;
    int i;
    int token;
    int generation;
    int previousGeneration;

    iterations = DEFAULT_ITERATIONS;
    if (argc == 2) {
        iterations = atoi(argv[1]);
    }
    if (iterations < 1 || iterations > MAX_ITERATIONS) {
        fprintf(stderr, "lease-loop: iterations must be 1..%d\n",
                MAX_ITERATIONS);
        return 2;
    }

    service = PORT_NULL;
    clientPort = PORT_NULL;
    kr = netname_look_up(name_server_port, "", OPENSTEP_MGA_SERVICE_NAME,
                         &service);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "lease-loop: lookup=%d\n", (int)kr);
        return 3;
    }

    kr = OSMGAAllocateClientPort(&clientPort);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "lease-loop: client-port=%d\n", (int)kr);
        return 4;
    }

    previousGeneration = 0;
    for (i = 0; i < iterations; i++) {
        token = 0;
        generation = 0;
        kr = OSMGA_acquire(service, OSMGA_PROTOCOL_VERSION, clientPort,
                           &token, &generation);
        if (kr != KERN_SUCCESS || token == 0 || generation == 0 ||
            generation == previousGeneration) {
            fprintf(stderr,
                    "lease-loop: acquire i=%d result=%d token=%d generation=%d\n",
                    i, (int)kr, token, generation);
            return 5;
        }

        kr = OSMGA_release(service, token, generation);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr,
                    "lease-loop: release i=%d result=%d token=%d generation=%d\n",
                    i, (int)kr, token, generation);
            return 6;
        }
        previousGeneration = generation;
    }

    printf("OPENSTEP_MGA_LEASE_LOOP iterations=%d final_generation=%d result=0\n",
           iterations, previousGeneration);
    OSMGADeallocateClientPort(clientPort);
    return 0;
}
