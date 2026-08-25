/*
 * What it would cost to carry a batch's vertices with it.
 *
 * Submissions cost 85.4 us of fixed overhead each and there are 33.5 of them
 * a frame, all closed by a render bracket rather than by the batch limit.
 * Merging brackets is worth (33.5 - 11) * 85.4 = 1.92 ms of a 16.8 ms frame,
 * and the thing in the way is that a refused batch replays through Mesa by VB
 * INDEX -- so carrying a batch across a bracket means carrying the vertices.
 *
 * The obvious way to price that is to put the copy in the builder and watch
 * the frame time.  It does not work: frame time on this machine scatters by
 * about 0.3 ms run to run, and the number being chased is between 0.1 and
 * 1.0 ms.  Measured, not assumed -- five runs of the profiler came back
 * 16.60, 16.69, 16.76, 16.78, 17.45.
 *
 * So this measures the copy on its own, at the size and in the shape the
 * real one would have: three vertices a triangle, each taken from three
 * separate arrays the way OSMGA_LOAD takes them (Win four floats, colour
 * four bytes, texture four floats), scattered across a vertex buffer rather
 * than walked in order, into one packed record.
 *
 * What it does NOT price: the replay reading the copy back, the private
 * vertex buffer such a replay would need, and the observation boundary the
 * mirror keeps at RenderFinish.  A floor, not an estimate.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define VERTS    1024          /* a vertex buffer's worth */
#define TRIS      988          /* source triangles in the measured frame */
#define ROUNDS    200        /* argv[1] overrides: the timing must scale */

typedef struct { float x, y, z, w; } Win4;
typedef unsigned char Col4[4];
typedef struct { float s, t, r, q; } Tex4;

/* what a carried vertex would have to hold: 36 bytes, 108 a triangle */
typedef struct {
    float x, y, z, w;
    unsigned char r, g, b, a;
    float s, t, tr, q;
} Carried;

static Win4 win[VERTS];
static Col4 col[VERTS];
static Tex4 tex[VERTS];
static unsigned int idx[TRIS][3];
static Carried out[TRIS][3];

static double
now(void)
{
    struct timeval tv;
    gettimeofday(&tv, (struct timezone *)0);
    return (double)tv.tv_sec + (double)tv.tv_usec / 1000000.0;
}

int
main(int argc, char **argv)
{
    int i, j, k, r;
    int rounds = (argc > 1) ? atoi(argv[1]) : ROUNDS;
    double t0, t1, per, bytes;
    unsigned int seed = 12345U;

    for (i = 0; i < VERTS; i++) {
        win[i].x = (float)i; win[i].y = (float)(i * 3);
        win[i].z = 0.5f;     win[i].w = 1.0f;
        col[i][0] = (unsigned char)i; col[i][1] = 0; col[i][2] = 255;
        col[i][3] = 255;
        tex[i].s = 0.25f; tex[i].t = 0.75f; tex[i].r = 0.0f; tex[i].q = 1.0f;
    }
    /* Indices scattered the way a mesh's are, not walked in order: a strip
     * revisits vertices and a fan comes back to its hub. */
    for (i = 0; i < TRIS; i++)
        for (j = 0; j < 3; j++) {
            seed = seed * 1103515245U + 12345U;
            idx[i][j] = (seed >> 16) % VERTS;
        }

    /* one untimed pass so the arrays are resident */
    for (i = 0; i < TRIS; i++)
        for (j = 0; j < 3; j++)
            out[i][j].x = win[idx[i][j]].x;

    t0 = now();
    for (r = 0; r < rounds; r++) {
        for (i = 0; i < TRIS; i++) {
            for (j = 0; j < 3; j++) {
                k = (int)idx[i][j];
                out[i][j].x = win[k].x;
                out[i][j].y = win[k].y;
                out[i][j].z = win[k].z;
                out[i][j].w = win[k].w;
                out[i][j].r = col[k][0];
                out[i][j].g = col[k][1];
                out[i][j].b = col[k][2];
                out[i][j].a = col[k][3];
                out[i][j].s  = tex[k].s;
                out[i][j].t  = tex[k].t;
                out[i][j].tr = tex[k].r;
                out[i][j].q  = tex[k].q;
            }
        }
    }
    t1 = now();

    /* read the result so no part of the loop is dead */
    {
        double sum = 0.0;
        for (i = 0; i < TRIS; i++)
            sum += (double)out[i][0].x + (double)out[i][2].q;
        if (sum == 12345.678) printf("");
    }

    per = (t1 - t0) / (double)rounds * 1000.0;
    bytes = (double)TRIS * 3.0 * (double)sizeof(Carried);
    printf("carrying a frame's vertices\n\n");
    printf("  %d triangles, %d bytes a vertex, %.1f KB a frame\n",
           TRIS, (int)sizeof(Carried), bytes / 1024.0);
    printf("  %d rounds in %.3f s\n", rounds, t1 - t0);
    printf("  %.3f ms a frame, %.1f MB/s\n",
           per, bytes / (per / 1000.0) / (1024.0 * 1024.0));
    printf("\n  the lever it comes out of is 1.92 ms; this is %.0f%% of it\n",
           100.0 * per / 1.922);
    printf("  (a floor: the replay reading it back, the private vertex\n");
    printf("   buffer it would need, and the mirror boundary are not here)\n");
    return 0;
}
