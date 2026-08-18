/*
 * openstep-mga-namecheck.c - read-only Network Name Server collision check.
 *
 * This is a user-space P2 preflight test.  It never creates, registers, or
 * destroys a Mach port; it only asks whether the planned SMAP/ADVERTISE name
 * is already present in the local name space.
 */

#include <mach/mach.h>
#include <servers/netname.h>
#include <servers/netname_defs.h>
#include <stdio.h>

#define OPENSTEP_MGA_SERVICE_NAME "openstepmga0"

int
main(void)
{
    kern_return_t kr;
    port_t port;

    port = PORT_NULL;
    kr = netname_look_up(name_server_port, "", OPENSTEP_MGA_SERVICE_NAME,
                         &port);

    printf("OPENSTEP_MGA_NAMECHECK name=%s result=%d port=%d\n",
           OPENSTEP_MGA_SERVICE_NAME, (int)kr, (int)port);

    if (kr == KERN_SUCCESS) {
        fprintf(stderr, "namecheck: service name is already registered\n");
        return 1;
    }
    if (kr == NETNAME_NOT_CHECKED_IN) {
        return 0;
    }

    fprintf(stderr, "namecheck: lookup failed unexpectedly\n");
    return 2;
}
