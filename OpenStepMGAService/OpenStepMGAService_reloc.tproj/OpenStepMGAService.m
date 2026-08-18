/*
 * P2.3 MiG server implementation.
 *
 * This file deliberately has no DriverKit device class, no PCI access, no
 * physical/virtual mapping, and no MGA register definitions.  It can only
 * prove that the control-plane port and generated MiG dispatch work.
 */

#import <mach/mach.h>
#import <mach/machine/simple_lock.h>
#import <driverkit/generalFuncs.h>
#import <kernserv/kern_server_types.h>
#import "OpenStepMGAProtocol.h"

extern kern_server_t OpenStepMGAService_instance;

/* IODevice.h remaps this legacy Mach entry point to the exported suffix. */
extern kern_return_t port_deallocate_EXTERNAL(task_t task,
                                             port_name_t port_name);

static simple_lock_data_t leaseLock;
static unsigned int nextToken;
static unsigned int activeToken;
static unsigned int activeGeneration;
static int leaseActive;
static port_t activeClientPort;

/* Called by the documented CALL load command before SMAP is used. */
void
openStepMGAServiceInit(int unused)
{
    (void)unused;

    simple_lock_init(&leaseLock);
    nextToken = 1;
    activeToken = 0;
    activeGeneration = 0;
    leaseActive = 0;
    activeClientPort = PORT_NULL;
    IOLog("OpenStepMGAService: P2.3 software lease initialized\n");
}

/* Called by the documented PORT_DEATH load-command hook. */
boolean_t
openStepMGAServicePortDeath(port_name_t port)
{
    int released;

    released = 0;
    simple_lock(&leaseLock);
    if (leaseActive && activeClientPort == port) {
        activeToken = 0;
        leaseActive = 0;
        activeClientPort = PORT_NULL;
        released = 1;
    }
    simple_unlock(&leaseLock);

    /* The notification has already been delivered; release our dead name. */
    (void)port_deallocate((task_t)kern_serv_kernel_task_port(), port);
    if (released) {
        IOLog("OpenStepMGAService: P2.3 client port died; lease released\n");
    }
    return TRUE;
}

kern_return_t
OSMGA_protocol_info(port_t server_port, int client_protocol,
                    int *service_protocol, int *feature_flags)
{
    (void)server_port;

    if (service_protocol == 0 || feature_flags == 0) {
        return KERN_INVALID_ARGUMENT;
    }

    *service_protocol = OSMGA_PROTOCOL_VERSION;
    *feature_flags = OSMGA_P2_FEATURES;

    if (client_protocol != OSMGA_PROTOCOL_VERSION) {
        IOLog("OpenStepMGAService: protocol version mismatch %d\n",
              client_protocol);
        return KERN_INVALID_ARGUMENT;
    }

    IOLog("OpenStepMGAService: P2.3 protocol_info\n");
    return KERN_SUCCESS;
}

kern_return_t
OSMGA_query_capabilities(port_t server_port, int client_protocol,
                         int *feature_flags, int *max_leases,
                         int *hardware_ready)
{
    (void)server_port;

    if (feature_flags == 0 || max_leases == 0 || hardware_ready == 0 ||
        client_protocol != OSMGA_PROTOCOL_VERSION) {
        return KERN_INVALID_ARGUMENT;
    }

    *feature_flags = OSMGA_P2_FEATURES;
    *max_leases = OSMGA_MAX_LEASES;
    *hardware_ready = OSMGA_HARDWARE_READY;
    IOLog("OpenStepMGAService: P2.3 query_capabilities\n");
    return KERN_SUCCESS;
}

kern_return_t
OSMGA_acquire(port_t server_port, int client_protocol, port_t client_port,
              int *token, int *generation)
{
    kern_return_t notifyResult;
    unsigned int issuedToken;
    unsigned int issuedGeneration;

    (void)server_port;

    if (token == 0 || generation == 0 || client_port == PORT_NULL ||
        client_protocol != OSMGA_PROTOCOL_VERSION) {
        if (client_port != PORT_NULL) {
            (void)port_deallocate((task_t)kern_serv_kernel_task_port(), client_port);
        }
        return KERN_INVALID_ARGUMENT;
    }

    *token = 0;
    *generation = 0;

    simple_lock(&leaseLock);
    if (leaseActive) {
        simple_unlock(&leaseLock);
        (void)port_deallocate((task_t)kern_serv_kernel_task_port(), client_port);
        return KERN_RESOURCE_SHORTAGE;
    }

    notifyResult = kern_serv_notify(
        &OpenStepMGAService_instance,
        kern_serv_notify_port(&OpenStepMGAService_instance), client_port);
    if (notifyResult != KERN_SUCCESS) {
        simple_unlock(&leaseLock);
        (void)port_deallocate((task_t)kern_serv_kernel_task_port(), client_port);
        IOLog("OpenStepMGAService: P2.3 client port notify failed\n");
        return notifyResult;
    }

    issuedToken = nextToken++;
    if (issuedToken == 0) {
        issuedToken = nextToken++;
    }
    if (nextToken == 0) {
        nextToken = 1;
    }

    issuedGeneration = activeGeneration + 1;
    if (issuedGeneration == 0) {
        issuedGeneration = 1;
    }

    activeToken = issuedToken;
    activeGeneration = issuedGeneration;
    activeClientPort = client_port;
    leaseActive = 1;
    simple_unlock(&leaseLock);

    *token = (int)issuedToken;
    *generation = (int)issuedGeneration;
    IOLog("OpenStepMGAService: P2.3 lease acquired\n");
    return KERN_SUCCESS;
}

kern_return_t
OSMGA_release(port_t server_port, int token, int generation)
{
    port_t clientPort;

    (void)server_port;

    if (token == 0 || generation == 0) {
        return KERN_INVALID_ARGUMENT;
    }

    simple_lock(&leaseLock);
    if (!leaseActive || activeToken != (unsigned int)token ||
        activeGeneration != (unsigned int)generation) {
        simple_unlock(&leaseLock);
        return KERN_INVALID_ARGUMENT;
    }

    clientPort = activeClientPort;
    activeToken = 0;
    leaseActive = 0;
    activeClientPort = PORT_NULL;
    simple_unlock(&leaseLock);

    /* Cancel the notification before dropping the copied send right. */
    kern_serv_port_gone(&OpenStepMGAService_instance, clientPort);
    (void)port_deallocate((task_t)kern_serv_kernel_task_port(), clientPort);
    IOLog("OpenStepMGAService: P2.3 lease released\n");
    return KERN_SUCCESS;
}
