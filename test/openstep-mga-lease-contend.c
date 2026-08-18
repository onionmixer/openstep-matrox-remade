/*
 * P2.4 multi-client software-lease contention test.
 *
 * Run two or more independent instances concurrently.  Each instance accepts
 * KERN_RESOURCE_SHORTAGE only as a transient busy reply, retries until it has
 * completed the requested number of acquire/release pairs, and never sends
 * MGA hardware state.
 */

#include <mach/mach.h>
#include <servers/netname.h>
#include <stdio.h>
#include <stdlib.h>

#include "OpenStepMGAUser.h"
#include "OpenStepMGAProtocol.h"
#include "OpenStepMGAClientPort.h"

#define OPENSTEP_MGA_SERVICE_NAME "openstepmga0"
#define DEFAULT_SUCCESSES 250
#define MAX_SUCCESSES 10000

int
main(int argc, char **argv)
{
    kern_return_t kr;
    port_t service;
    port_t clientPort;
    int targetSuccesses;
    int completed;
    int busyReplies;
    int attempts;
    int attemptLimit;
    int token;
    int generation;
    int finalGeneration;

    targetSuccesses = DEFAULT_SUCCESSES;
    if (argc == 2) {
        targetSuccesses = atoi(argv[1]);
    }
    if (targetSuccesses < 1 || targetSuccesses > MAX_SUCCESSES) {
        fprintf(stderr, "lease-contend: successes must be 1..%d\n",
                MAX_SUCCESSES);
        return 1;
    }

    service = PORT_NULL;
    clientPort = PORT_NULL;
    completed = 0;
    busyReplies = 0;
    attempts = 0;
    attemptLimit = targetSuccesses * 10000;
    finalGeneration = 0;

    kr = netname_look_up(name_server_port, "", OPENSTEP_MGA_SERVICE_NAME,
                         &service);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "lease-contend: lookup=%d\n", (int)kr);
        return 2;
    }
    kr = OSMGAAllocateClientPort(&clientPort);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "lease-contend: client-port=%d\n", (int)kr);
        return 3;
    }

    while (completed < targetSuccesses) {
        token = 0;
        generation = 0;
        attempts++;
        if (attempts > attemptLimit) {
            fprintf(stderr,
                    "lease-contend: retry limit completed=%d busy=%d\n",
                    completed, busyReplies);
            OSMGADeallocateClientPort(clientPort);
            return 4;
        }

        kr = OSMGA_acquire(service, OSMGA_PROTOCOL_VERSION, clientPort,
                           &token, &generation);
        if (kr == KERN_RESOURCE_SHORTAGE) {
            busyReplies++;
            continue;
        }
        if (kr != KERN_SUCCESS || token == 0 || generation == 0 ||
            generation == finalGeneration) {
            fprintf(stderr,
                    "lease-contend: acquire completed=%d result=%d token=%d generation=%d\n",
                    completed, (int)kr, token, generation);
            OSMGADeallocateClientPort(clientPort);
            return 5;
        }

        kr = OSMGA_release(service, token, generation);
        if (kr != KERN_SUCCESS) {
            fprintf(stderr,
                    "lease-contend: release completed=%d result=%d token=%d generation=%d\n",
                    completed, (int)kr, token, generation);
            OSMGADeallocateClientPort(clientPort);
            return 6;
        }
        completed++;
        finalGeneration = generation;
    }

    printf("OPENSTEP_MGA_LEASE_CONTEND successes=%d busy=%d attempts=%d final_generation=%d result=0\n",
           completed, busyReplies, attempts, finalGeneration);
    OSMGADeallocateClientPort(clientPort);
    return 0;
}
