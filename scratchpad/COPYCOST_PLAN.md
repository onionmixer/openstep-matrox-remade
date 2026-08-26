# Plan: price the vertex copy before designing anything around it

## The decision this feeds

Submissions cost 85.4 us of fixed overhead each, measured, and there are 33.5
of them a frame -- 2.87 ms that no amount of list-shrinking can reach. They
are all caused by render brackets closing (flush counters: bracket 1952, key
0, full 0, other 0) and never by the batch limit, so merging brackets is the
lever. Merging to the batch limit's floor of 11 submissions would save
1.87 ms of a 16.86 ms frame -- 11%.

The reason batches stop at a bracket is the REPLAY CONTRACT, not performance.
A refused batch is redrawn in software, and the redraw calls Mesa's own
triangle function with VB INDICES:

    (*savedTriangle)(ctx, v0, v1, v2, pv)

Those indices are only meaningful while that VB is alive. Carry a batch past
the bracket and they are not.

## What a replay actually needs, read off the builder

The builder's vertex macro reads exactly:

    VB->Win.data[idx][0..3]            4 GLfloat   16 bytes
    VB->ColorPtr->data[idx][0..3]      4 GLubyte    4 bytes
    VB->TexCoordPtr[0]->data[idx][0..3] 4 GLfloat  16 bytes  (when textured)

36 bytes a vertex, 108 a triangle, 988 triangles a frame -> 104 KB a frame.

Estimated cost, which is why this has to be measured rather than argued:

    100 MB/s -> 1.02 ms   54% of the gain
    200 MB/s -> 0.51 ms   27%
    400 MB/s -> 0.25 ms   14%
    800 MB/s -> 0.13 ms    7%

Between "the lever is dead" and "the lever is nearly free".

## The measurement

A test-only knob in the Mesa hook, off by default. When on, the place that
already records a source triangle's indices also copies those three vertices'
attributes into a static array of the same capacity. Nothing reads the copy.
The frame-time difference with the knob on and off is the price.

    frame at knob off  -  frame at knob on   =  what copying costs

Userland only, no reboot, no change to any output. The scene baselines and
the byte-identity check must be unmoved with the knob on, which also proves
the copy is inert.

## What this does and does not establish

It establishes a LOWER BOUND on what the redesign costs. Copying is only part
of it: the copies have to end up somewhere a software triangle can read, and
Mesa's rasteriser reads a VB. Pricing that is a separate question this does
not touch.

So the decision rule is one-sided. If the lower bound already eats most of
1.87 ms, the lever is dead and I stop. If it is small, the lever stays open
and the rest of the redesign still has to be priced.

## Questions

1. Is the attribute list right -- does a software replay of these triangles
   read anything else per vertex that would have to be carried?
2. Is measuring the copy in the triangle tail the right place, or does that
   put it somewhere the real design would not pay it?
3. Is there a cheaper way to reach the same decision that I have missed?
4. What would make this measurement lie -- for instance the copy being
   optimised away, or landing in cache in a way the real thing would not?
