/* P2.3 user-side smoke test: capabilities plus software lease semantics. */

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
    int protocol;
    int features;
    int maxLeases;
    int hardwareReady;
    int token;
    int generation;
    int secondToken;
    int secondGeneration;

    service = PORT_NULL;
    clientPort = PORT_NULL;
    protocol = 0;
    features = 0;
    maxLeases = 0;
    hardwareReady = -1;
    token = 0;
    generation = 0;
    secondToken = 0;
    secondGeneration = 0;

    kr = netname_look_up(name_server_port, "", OPENSTEP_MGA_SERVICE_NAME,
                         &service);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "protocol-smoke: lookup=%d\n", (int)kr);
        return 1;
    }

    kr = OSMGAAllocateClientPort(&clientPort);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "protocol-smoke: client-port=%d\n", (int)kr);
        return 2;
    }

    kr = OSMGA_protocol_info(service, OSMGA_PROTOCOL_VERSION,
                             &protocol, &features);
    printf("OPENSTEP_MGA_PROTOCOL result=%d protocol=%d features=%08x\n",
           (int)kr, protocol, (unsigned int)features);

    if (kr != KERN_SUCCESS || protocol != OSMGA_PROTOCOL_VERSION ||
        features != OSMGA_P2_FEATURES) {
        return 3;
    }

    kr = OSMGA_query_capabilities(service, OSMGA_PROTOCOL_VERSION,
                                  &features, &maxLeases, &hardwareReady);
    printf("OPENSTEP_MGA_CAPABILITIES result=%d features=%08x max_leases=%d hardware_ready=%d\n",
           (int)kr, (unsigned int)features, maxLeases, hardwareReady);
    if (kr != KERN_SUCCESS || features != OSMGA_P2_FEATURES ||
        maxLeases != OSMGA_MAX_LEASES ||
        hardwareReady != OSMGA_HARDWARE_READY) {
        return 4;
    }

    kr = OSMGA_query_capabilities(service, OSMGA_PROTOCOL_VERSION + 1,
                                  &features, &maxLeases, &hardwareReady);
    printf("OPENSTEP_MGA_CAPABILITIES_BAD_VERSION result=%d\n", (int)kr);
    if (kr != KERN_INVALID_ARGUMENT) {
        return 5;
    }

    kr = OSMGA_protocol_info(service, OSMGA_PROTOCOL_VERSION + 1,
                             &protocol, &features);
    printf("OPENSTEP_MGA_PROTOCOL_BAD_VERSION result=%d\n", (int)kr);
    if (kr != KERN_INVALID_ARGUMENT) {
        return 6;
    }

    kr = OSMGA_acquire(service, OSMGA_PROTOCOL_VERSION + 1, clientPort,
                       &token, &generation);
    printf("OPENSTEP_MGA_ACQUIRE_BAD_VERSION result=%d\n", (int)kr);
    if (kr != KERN_INVALID_ARGUMENT) {
        return 7;
    }

    kr = OSMGA_acquire(service, OSMGA_PROTOCOL_VERSION, clientPort,
                       &token, &generation);
    printf("OPENSTEP_MGA_ACQUIRE result=%d token=%d generation=%d\n",
           (int)kr, token, generation);
    if (kr != KERN_SUCCESS || token == 0 || generation == 0) {
        return 8;
    }

    kr = OSMGA_acquire(service, OSMGA_PROTOCOL_VERSION, clientPort,
                       &secondToken, &secondGeneration);
    printf("OPENSTEP_MGA_ACQUIRE_BUSY result=%d\n", (int)kr);
    if (kr != KERN_RESOURCE_SHORTAGE) {
        return 9;
    }

    kr = OSMGA_release(service, token, generation + 1);
    printf("OPENSTEP_MGA_RELEASE_STALE result=%d\n", (int)kr);
    if (kr != KERN_INVALID_ARGUMENT) {
        return 10;
    }

    kr = OSMGA_release(service, token, generation);
    printf("OPENSTEP_MGA_RELEASE result=%d\n", (int)kr);
    if (kr != KERN_SUCCESS) {
        return 11;
    }

    kr = OSMGA_acquire(service, OSMGA_PROTOCOL_VERSION, clientPort,
                       &secondToken, &secondGeneration);
    printf("OPENSTEP_MGA_REACQUIRE result=%d token=%d generation=%d\n",
           (int)kr, secondToken, secondGeneration);
    if (kr != KERN_SUCCESS || secondToken == 0 || secondGeneration == 0 ||
        secondGeneration == generation) {
        return 12;
    }

    kr = OSMGA_release(service, secondToken, secondGeneration);
    printf("OPENSTEP_MGA_RERELEASE result=%d\n", (int)kr);
    if (kr != KERN_SUCCESS) {
        return 13;
    }
    OSMGADeallocateClientPort(clientPort);
    return 0;
}
