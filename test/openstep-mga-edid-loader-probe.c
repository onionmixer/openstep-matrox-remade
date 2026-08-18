/* Target loader probe: link D0 object without invoking parser logic. */

#include <stdio.h>

#ifdef OSMGA_TARGET_NETNAME_BOOTSTRAP
#include <mach/mach.h>
#include <servers/netname.h>

static void
bootstrap_netname_lookup(void)
{
    port_t port;

    port = PORT_NULL;
    (void)netname_look_up(name_server_port, "", "openstepmga-d0-bootstrap",
                          &port);
}
#endif

int
main(void)
{
#ifdef OSMGA_TARGET_NETNAME_BOOTSTRAP
    bootstrap_netname_lookup();
#endif
    /*
     * The target's known-good P2 user client imports the legacy stdio FILE
     * runtime through fprintf/__iob.  Keep this probe's process shape equal
     * to the full policy test before drawing a conclusion about D0 linkage.
     * This is only an observable stderr marker; it has no device dependency.
     */
    fprintf(stderr, "OPENSTEP_MGA_D0_LOADER_STDIO=pass\n");
    printf("OPENSTEP_MGA_D0_LOADER_PROBE=pass\n");
    return 0;
}
