# The restore works: the console came back at shutdown

## What happened

With OSMGAVgaRestore set to 1, the machine was rebooted and the user reports
the low-resolution console screen came back during shutdown -- the thing that
had been black since the driver started programming the card fully.

So hypothesis A holds: -revertToVGAMode IS called on the shutdown path, even
though it is NOT called when the WindowServer dies and restarts (a logout
moved neither enterLinear nor revertVGA, and printed no V2 line).

The V2 log line is not on disk, because syslogd is stopped before that point
in a shutdown ("syslogd: going down on signal 15" precedes the reboot in
every boot's log). The evidence is the screen.

## What that establishes and what it does not

Established: the register set, the order and the eligibility test are right
for this console, on this card, in the situation the symptom is about.

Not established: that the restore is correct for a console in some other
mode. The snapshot is whatever the machine had; the eligibility test refuses
text because no font planes were saved. A machine that boots its console in
text mode would get the refusal and today's black screen, not a wrong
picture.

## The obvious next step

Promote OSMGAVgaRestore to default on. It is one initialiser. Until then the
fix only exists on a machine where somebody has run "vgarestore 1" since the
last boot, which is nobody's idea of a fix.

## What I would want the promotion to keep

- the eligibility test, so a text console still refuses rather than guesses;
- the engine claim check, so a wedged engine skips the register writes;
- the lifecycle running on every path.

## Questions

1. Is there any reason NOT to default this on, given it is proven on the one
   path that matters and refuses itself everywhere else?
2. The restore now runs on every revert, which includes the one during driver
   initialisation -- before the first -programLinearMode, when no snapshot
   exists yet. Confirm that path is a no-op and cannot be reached with a
   half-filled snapshot.
3. Anything about making it default-on that changes the risk of the boot
   path, as opposed to the shutdown path?
