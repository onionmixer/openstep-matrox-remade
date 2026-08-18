/*
 * P2.5 raw MiG negative test.
 *
 * This deliberately bypasses the generated client stub only to exercise the
 * generated server's fixed-size/type checks.  It sends no OOL data, no port
 * descriptor, no MGA state, and no request that can acquire a lease.
 */

#include <mach/mach.h>
#include <mach/message.h>
#include <mach/mig_errors.h>
#include <mach/msg_type.h>
#include <servers/netname.h>
#include <stdio.h>

#include "OpenStepMGAUser.h"
#include "OpenStepMGAProtocol.h"

#define OPENSTEP_MGA_SERVICE_NAME "openstepmga0"
#define OSMGA_QUERY_CAPABILITIES_ID 4101
#define OSMGA_QUERY_CAPABILITIES_REPLY_ID 4201

typedef struct {
    msg_header_t Head;
    msg_type_t clientProtocolType;
    int clientProtocol;
} OSMGARawQueryRequest;

typedef struct {
    msg_header_t Head;
    msg_type_t RetCodeType;
    kern_return_t RetCode;
} OSMGARawErrorReply;

static kern_return_t
sendRawQuery(port_t service, port_t replyPort, unsigned int messageSize,
             msg_type_t clientProtocolType, const char *label)
{
    union {
        OSMGARawQueryRequest request;
        OSMGARawErrorReply reply;
    } message;
    msg_return_t mr;

    message.request.Head.msg_simple = TRUE;
    message.request.Head.msg_size = messageSize;
    message.request.Head.msg_type = MSG_TYPE_NORMAL | MSG_TYPE_RPC;
    message.request.Head.msg_remote_port = service;
    message.request.Head.msg_local_port = replyPort;
    message.request.Head.msg_id = OSMGA_QUERY_CAPABILITIES_ID;
    message.request.clientProtocolType = clientProtocolType;
    message.request.clientProtocol = OSMGA_PROTOCOL_VERSION;

    mr = msg_rpc(&message.request.Head, MSG_OPTION_NONE,
                 sizeof(OSMGARawErrorReply), 0, 0);
    printf("OPENSTEP_MGA_RAW_%s_TRANSPORT result=%d reply_id=%d reply_size=%u reply_code=%d\n",
           label, (int)mr, (int)message.reply.Head.msg_id,
           (unsigned int)message.reply.Head.msg_size,
           (int)message.reply.RetCode);
    if (mr != RPC_SUCCESS ||
        message.reply.Head.msg_id != OSMGA_QUERY_CAPABILITIES_REPLY_ID ||
        message.reply.Head.msg_size != sizeof(OSMGARawErrorReply) ||
        message.reply.RetCode != MIG_BAD_ARGUMENTS) {
        return KERN_FAILURE;
    }
    return KERN_SUCCESS;
}

int
main(void)
{
    kern_return_t kr;
    port_t service;
    port_t replyPort;
    msg_type_t validType;
    msg_type_t wrongType;
    int features;
    int maxLeases;
    int hardwareReady;

    service = PORT_NULL;
    replyPort = PORT_NULL;
    features = 0;
    maxLeases = 0;
    hardwareReady = -1;

    kr = netname_look_up(name_server_port, "", OPENSTEP_MGA_SERVICE_NAME,
                         &service);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "raw-mig-negative: lookup=%d\n", (int)kr);
        return 1;
    }
    kr = port_allocate(task_self(), &replyPort);
    if (kr != KERN_SUCCESS) {
        fprintf(stderr, "raw-mig-negative: reply-port=%d\n", (int)kr);
        return 2;
    }

    validType.msg_type_name = MSG_TYPE_INTEGER_32;
    validType.msg_type_size = 32;
    validType.msg_type_number = 1;
    validType.msg_type_inline = TRUE;
    validType.msg_type_longform = FALSE;
    validType.msg_type_deallocate = FALSE;
    validType.msg_type_unused = 0;

    kr = sendRawQuery(service, replyPort, sizeof(msg_header_t), validType,
                      "SHORT");
    if (kr != KERN_SUCCESS) {
        (void)port_deallocate(task_self(), replyPort);
        return 3;
    }

    wrongType = validType;
    wrongType.msg_type_name = MSG_TYPE_INTEGER_16;
    kr = sendRawQuery(service, replyPort, sizeof(OSMGARawQueryRequest),
                      wrongType, "WRONG_TYPE");
    if (kr != KERN_SUCCESS) {
        (void)port_deallocate(task_self(), replyPort);
        return 4;
    }

    kr = OSMGA_query_capabilities(service, OSMGA_PROTOCOL_VERSION,
                                  &features, &maxLeases, &hardwareReady);
    printf("OPENSTEP_MGA_RAW_RECOVERY result=%d features=%08x max_leases=%d hardware_ready=%d\n",
           (int)kr, (unsigned int)features, maxLeases, hardwareReady);
    (void)port_deallocate(task_self(), replyPort);
    if (kr != KERN_SUCCESS || features != OSMGA_P2_FEATURES ||
        maxLeases != OSMGA_MAX_LEASES ||
        hardwareReady != OSMGA_HARDWARE_READY) {
        return 5;
    }
    return 0;
}
