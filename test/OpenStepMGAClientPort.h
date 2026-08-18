/* Shared legacy-Mach owner-port helpers for P2.2 test clients. */

#ifndef OPENSTEP_MGA_CLIENT_PORT_H
#define OPENSTEP_MGA_CLIENT_PORT_H

#include <mach/mach.h>

static kern_return_t
OSMGAAllocateClientPort(port_t *clientPort)
{
    if (clientPort == 0) {
        return KERN_INVALID_ARGUMENT;
    }
    *clientPort = PORT_NULL;
    return port_allocate(task_self(), clientPort);
}

#ifndef OSMGA_CLIENT_PORT_NO_DEALLOCATE
static void
OSMGADeallocateClientPort(port_t clientPort)
{
    if (clientPort != PORT_NULL) {
        (void)port_deallocate(task_self(), clientPort);
    }
}
#endif

#endif /* OPENSTEP_MGA_CLIENT_PORT_H */
