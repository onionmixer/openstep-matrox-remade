#!/usr/bin/env python3
"""
Score a blended textured scene, by class rather than by count.

Each scene is drawn four times and the four dumps come in here:

    Ahw   blending off, through the engine    the source the blend consumes
    Asw   blending off, software              the same, Mesa's answer
    Bhw   blending on,  through the engine
    Bsw   blending on,  software

The unblended pass is the ORACLE'S INPUT, not a second opinion.  Using it
means the geometry, the sampling and the addend ladder are never modelled
here -- they are held by the ten scene baselines, and what this file asks
about is the blend arithmetic alone.

What that design cannot see is the two passes being wrong the same way: a
blend consistent with a badly sampled source still looks consistent.  So
Ahw == Asw is a PREREQUISITE.  If the two unblended passes disagree, nothing
below is scored at all, because the fault is upstream of the question.

There is no tolerance.  Each path is scored against its OWN arithmetic
exactly -- the engine's product for the engine, Mesa's for Mesa -- so a
one-level error on every pixel, which is what a wrong selector looks like,
cannot pass as rounding.

And each path is scored against ITS OWN unblended pass, not the other one's.
The two unblended passes are NOT required to agree, and requiring it was
wrong: the engine's normalised product is not Mesa's, so a modulated source
differs between them by construction -- the existing modg scene differs on
8516 of its pixels and always has.  Even replace differs a little, at the
sampling boundaries the ten scene baselines record.  Scoring each side
against its own source means that difference cannot contaminate the answer
to the question actually being asked, which is whether the blend arithmetic
is right given what that path drew.

What that leaves uncovered is the two passes being wrong in the same way, and
the answer to it is not in this file: the unblended passes are what the ten
scene baselines already hold, and the source difference between the paths is
reported here so that a change in it is visible rather than absorbed.
"""
import sys


def load(path):
    px = {}
    for line in open(path):
        if not line.startswith('P '):
            continue
        _, x, y, v = line.split()
        px[(int(x), int(y))] = int(v)
    return px


def argb(v):
    return ((v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF)


def engine_blend(src, dst):
    """
    What the engine puts down for SRC_ALPHA / ONE_MINUS_SRC_ALPHA.

        (Cs * t + Cd * (255 - t) + 127) / 255

    Fitted, not assumed: over the four blend scenes that is 262144 channel
    samples with not one disagreement, against seven candidate forms.  The
    same numbers rule out rounding each product separately -- the engine's own
    normalised product (x * a * 257 + 32768) >> 16 applied to each term and
    summed is wrong on 66514 of them.  So the engine multiplies twice, adds,
    and rounds ONCE.

    ((s*t + d*(255-t)) * 257 + 32768) >> 16 fits equally well and is the same
    function over this range; the division is written out because that is
    what it means.

    Mesa computes the same expression and TRUNCATES (blend.c,
    blend_transparency).  That one difference is the whole of the engine's
    disagreement with software here, and it is at most one level.

    The alpha channel takes the same factors as the colour ones, which is what
    GL asks for -- not the source-over compositing alpha.  See the note on
    OSMGAReferenceBlendSrcAlpha32, which computes the other one.
    """
    t = src[0]
    return tuple((s * t + d * (255 - t) + 127) // 255
                 for s, d in zip(src, dst))


def mesa_blend(src, dst):
    """
    Mesa-3.4.2/src/blend.c, blend_transparency -- the routine it installs for
    exactly this pair of factors, not the general path.

        (Cs * t + Cd * (255 - t)) / 255       integer, truncated

    with t the source alpha, and the alpha channel treated like any other,
    which is what GL asks for.  Three of the variants in that function are
    behind ifdefs; this is the one that matches the hardware's software twin
    on every channel of every pixel tried -- the shift variant gives 135
    where the measurement and this give 136.

    The two exact cases are separate there and are reproduced here: a source
    alpha of nought leaves the destination untouched, and 255 replaces it.
    """
    t = src[0]
    if t == 0:
        return tuple(dst)
    if t == 255:
        return tuple(src)
    return tuple((s * t + d * (255 - t)) // 255 for s, d in zip(src, dst))


def main():
    if len(sys.argv) != 6:
        print("usage: blend-compare.py NAME Ahw Asw Bhw Bsw")
        return 2
    name, ahw, asw, bhw, bsw = sys.argv[1:]
    A, As_, B, Bs = (load(ahw), load(asw), load(bhw), load(bsw))
    DEST = (0xA0, 0x20, 0x40, 0x60)      # what the scene painted

    if not A or not B:
        print("%-12s NOT RUN: an unblended or blended dump is empty" % name)
        return 1

    # -- the source difference, reported and not judged here
    srccov = len([k for k in set(A) | set(As_) if (k in A) != (k in As_)])
    srcdiff = len([k for k in A if k in As_ and A[k] != As_[k]])
    if srccov:
        print("%-12s NOT SCORED: the unblended passes cover different pixels"
              " (%d)" % (name, srccov))
        return 1

    #
    # -- the source ALPHA, which IS judged.
    #
    # The colour difference above stays reported and unjudged: the engine's
    # multiply is not Mesa's multiply and that is a recorded fact.  Alpha is
    # not that.  It is an interpolated plane with nothing to round but the
    # last bit, and the engine and Mesa agree on it -- so a disagreement of
    # more than one level is a defect and not a fact.
    #
    # It went unnoticed for as long as these scenes have existed because
    # this file counted the difference and scored each path against its own
    # source, which is exactly the arrangement in which an alpha 128 levels
    # out still comes to "ok" on every pixel.  M14: the second triangle of
    # this very quad was drawn from where the first one finished, because
    # the kernel's state tracking skipped an alpha start it thought had not
    # changed and the engine had advanced it.
    #
    # One level, not zero: the WARP tier truncates where the trapezoid tier
    # pre-adds half a level, and that difference is measured, understood
    # and unfixable through the vertex format (M12 section 8).
    #
    srcalpha = 0
    srcalphaAt = None
    for k in A:
        if k in As_:
            d = abs(((A[k] >> 24) & 0xFF) - ((As_[k] >> 24) & 0xFF))
            if d > srcalpha:
                srcalpha, srcalphaAt = d, k
    if srcalpha > 1:
        print("%-12s SOURCE ALPHA: worst %d levels from software at %s -- the"
              " unblended source alpha must agree within one"
              % (name, srcalpha, srcalphaAt))
        return 1

    klass = {'coverage': 0, 'engine': 0, 'mesa': 0, 'both': 0, 'ok': 0}
    worst = 0
    for k in set(A) | set(B) | set(Bs):
        if (k in A) != (k in B) or (k in A) != (k in Bs):
            klass['coverage'] += 1
            continue
        ge = engine_blend(argb(A[k]), DEST)      # the engine's own source
        me = mesa_blend(argb(As_[k]), DEST)      # and Mesa's own
        gb, mb = argb(B[k]), argb(Bs[k])
        bad_e, bad_m = gb != ge, mb != me
        if bad_e and bad_m:
            klass['both'] += 1
        elif bad_e:
            klass['engine'] += 1
            worst = max(worst, max(abs(p - q) for p, q in zip(gb, ge)))
        elif bad_m:
            klass['mesa'] += 1
        else:
            klass['ok'] += 1

    verdict = "ok" if (klass['coverage'] == 0 and klass['engine'] == 0
                       and klass['mesa'] == 0 and klass['both'] == 0) else "FAIL"
    print("%-12s %-4s  ok %5d  coverage %4d  engine %4d  mesa %4d  both %4d"
          "  worst %d   source hw/sw differ %d"
          % (name, verdict, klass['ok'], klass['coverage'], klass['engine'],
             klass['mesa'], klass['both'], worst, srcdiff))
    return 0 if verdict == "ok" else 1


if __name__ == '__main__':
    sys.exit(main())
