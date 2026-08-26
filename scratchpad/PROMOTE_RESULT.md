# The promotion is live, and the boot self-test now checks what ships

## The reboot took, and it proved the snapshot fix immediately

    previous boot   M1-2a: list 60 dwords (tail 55); MMIO drew 900, DMA drew 900; differing 0
    this boot       M1-2a: list 55 dwords (tail 50); MMIO drew 900, DMA drew 900; differing 0

Five dwords shorter is the FXBNDRY packing. Until this change the boot
diagnostics called the encoder directly, before any client, and read
snapshots that were still zero -- so with packing defaulted on they were
validating an encoding the driver no longer produces. The snapshots now start
where the settings do, and every boot checks DMA against MMIO on the
encoding it actually ships.

## Gates, with nothing set by hand

    frame                16.96 / 16.77 / 16.82 / 16.88 ms   -> 16.86 mean, 59.3 fps
    dwords per submission 1449.4      (tracking on)
    poll index max        293         (delay 4 on)
    scene baselines       SCENES_MOVED=0
    track byte-identity   20 scenes, 0 moved
    quick regression      PROBLEM 0, FAIL 0

## Each setting turned off live reproduces its own measurement

    configuration             frame ms      fps  dwords/sub  poll max  us/submit
    boot defaults (4,1,1)        16.86     59.3      1449.4       293      244.1
    delay off   (0,1,1)          17.96     55.7      1449.4      2064      277.5
    track off   (4,1,0)          18.41     54.3      1927.2       293      290.9
    both off    (0,1,0)          20.04     49.9      1927.2      2065      342.2

Against last reboot's figures: 17.96 vs 17.92 for delay-off (0.22% apart)
and 20.04 vs 19.97 for both-off (0.35% apart). So what changed is the
defaults and not something else, which is what this test existed to show.

## The session end to end, same scene

    session start              23.30 ms   42.9 fps
    after the userland work    20.50 ms   48.8 fps
    now, from boot             16.86 ms   59.3 fps      38.2% faster

## What I did NOT change

- rc3 = 1 on recovery success. It needs the settle read to move with it, and
  it fixes a case that has never occurred (real completion-poll timeouts:
  zero; the seven that ran were injected).
- The delay whitelist past 4. Flat between 2 and 4 (17.07 vs 16.86) and
  unmeasured beyond.

## Questions

1. Does anything in these numbers argue that something OTHER than the two
   defaults changed?
2. The boot self-test is a single-trapezoid batch, so it exercises packing
   but not the tracker's skip. Is there a cheap boot-time addition that would
   exercise the skip, or is the userland byte-identity check the right place
   for it to stay?
3. What is the next thing worth a reboot, if anything?
