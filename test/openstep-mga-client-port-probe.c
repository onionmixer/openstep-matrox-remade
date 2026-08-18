/* P2.2 prerequisite: verify client-owned Mach control-port lifecycle. */

#include <mach/mach.h>
#include <stdio.h>

int
main(void)
{
    kern_return_t kr;
    port_t control;

    control = PORT_NULL;
    /* OPENSTEP exports the legacy API; port_allocate grants all rights. */
    kr = port_allocate(task_self(), &control);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "client-port-probe: allocate=%d\n", (int)kr);
        return 1;
    }

    printf("OPENSTEP_MGA_CLIENT_PORT result=0 port=%d\n", (int)control);
    kr = port_deallocate(task_self(), control);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "client-port-probe: destroy=%d\n", (int)kr);
        return 2;
    }
    return 0;
}
