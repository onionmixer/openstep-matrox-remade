/*
 * The texture arena's allocator, on the host.
 *
 * What this has to be able to catch: a block handed out twice, a gap missed,
 * a stale epoch accepted, and a double free taken as a free.  Each case says
 * what it expects rather than printing what happened.
 */
#include <stdio.h>
#include <string.h>

#include "OpenStepMGAMesaTexArena.h"

#define ORG  0x400000UL
#define LEN  0x100000UL         /* one megabyte */

static int failures;

static void
say(const char *what, int ok)
{
    printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static int
overlaps(unsigned long a, unsigned long an, unsigned long b, unsigned long bn)
{
    return !(a + an <= b || b + bn <= a);
}

int
main(void)
{
    unsigned long o[8], n, used;
    int i, j;

    OSMGAMesaTexArenaSet(ORG, LEN, 7UL);

    say("a block comes out of the arena",
        OSMGAMesaTexAlloc(1000UL, 7UL, &o[0]) && o[0] >= ORG &&
        o[0] + 1000UL <= ORG + LEN);
    say("aligned", (o[0] % 64UL) == 0UL);

    for (i = 1; i < 5; i++)
        if (!OSMGAMesaTexAlloc(70000UL, 7UL, &o[i])) {
            say("four more blocks", 0);
            break;
        }
    {
        int clash = 0;

        for (i = 0; i < 5; i++)
            for (j = i + 1; j < 5; j++)
                if (overlaps(o[i], (i == 0) ? 1024UL : 70016UL,
                             o[j], (j == 0) ? 1024UL : 70016UL))
                    clash = 1;
        say("no two blocks overlap", !clash);
    }
    OSMGAMesaTexArenaStat(&n, &used);
    say("five blocks are recorded", n == 5UL);

    /* the gap left by a free must be reused */
    {
        unsigned long freed = o[2], again;

        say("a block frees", OSMGAMesaTexFree(freed, 7UL));
        say("freeing it again does not", !OSMGAMesaTexFree(freed, 7UL));
        say("the gap comes back",
            OSMGAMesaTexAlloc(70000UL, 7UL, &again) && again == freed);
        o[2] = again;
    }

    /* a stale epoch is refused, and a new one wipes the arena */
    {
        unsigned long p;

        say("a stale epoch cannot allocate", !OSMGAMesaTexAlloc(64UL, 6UL, &p));
        say("a stale epoch cannot free", !OSMGAMesaTexFree(o[0], 6UL));
        OSMGAMesaTexArenaSet(ORG, LEN, 8UL);
        OSMGAMesaTexArenaStat(&n, &used);
        say("a new epoch empties it", n == 0UL && used == 0UL);
        say("and the old blocks are not free-able",
            !OSMGAMesaTexFree(o[0], 7UL));
    }

    /* exhaustion */
    {
        unsigned long p;
        int got = 0;

        OSMGAMesaTexArenaSet(ORG, LEN, 9UL);
        while (OSMGAMesaTexAlloc(200000UL, 9UL, &p)) got++;
        say("exhaustion stops at the arena's size", got == 5);
        say("and says no rather than handing out the same place",
            !OSMGAMesaTexAlloc(200000UL, 9UL, &p));
        say("a small one still fits in what is left",
            OSMGAMesaTexAlloc(1000UL, 9UL, &p));
    }

    /* an empty arena */
    OSMGAMesaTexArenaSet(ORG, 0UL, 10UL);
    {
        unsigned long p;

        say("an empty arena hands out nothing",
            !OSMGAMesaTexAlloc(64UL, 10UL, &p));
    }

    printf("\n%s (%d failing)\n",
           failures ? "=== PROBLEM ===" : "=== nothing to report ===",
           failures);
    return failures ? 1 : 0;
}
