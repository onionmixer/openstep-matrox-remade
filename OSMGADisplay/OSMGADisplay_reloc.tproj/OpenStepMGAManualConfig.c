#include "OpenStepMGAManualConfig.h"

static int
osmgais_space(char value)
{
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

int
OSMGAParseManualMemoryMB(const char *value, unsigned int *bytes,
                         OSMGAManualMemoryStatus *status)
{
    unsigned int megabytes;
    int digit_seen;

    if (bytes == 0 || status == 0)
        return 0;
    *bytes = 0;
    *status = OSMGA_MANUAL_MEMORY_MISSING;
    if (value == 0)
        return 0;

    while (osmgais_space(*value))
        value++;
    if (*value == '\0')
        return 0;

    megabytes = 0;
    digit_seen = 0;
    while (*value >= '0' && *value <= '9') {
        digit_seen = 1;
        if (megabytes > 63U) {
            *status = OSMGA_MANUAL_MEMORY_INVALID;
            return 0;
        }
        megabytes = megabytes * 10U + (unsigned int)(*value - '0');
        value++;
    }
    while (osmgais_space(*value))
        value++;
    if (!digit_seen || *value != '\0') {
        *status = OSMGA_MANUAL_MEMORY_INVALID;
        return 0;
    }
    if (megabytes < 3U || megabytes > 63U) {
        *status = OSMGA_MANUAL_MEMORY_UNSUPPORTED;
        return 0;
    }

    *bytes = megabytes * 1024U * 1024U;
    *status = OSMGA_MANUAL_MEMORY_OK;
    return 1;
}

const char *
OSMGAManualMemoryStatusString(OSMGAManualMemoryStatus status)
{
    switch (status) {
    case OSMGA_MANUAL_MEMORY_OK:
        return "ok";
    case OSMGA_MANUAL_MEMORY_MISSING:
        return "missing";
    case OSMGA_MANUAL_MEMORY_INVALID:
        return "invalid";
    case OSMGA_MANUAL_MEMORY_UNSUPPORTED:
        return "unsupported";
    default:
        return "unknown";
    }
}
