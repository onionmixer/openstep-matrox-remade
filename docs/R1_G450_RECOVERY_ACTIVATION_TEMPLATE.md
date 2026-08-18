# R1 — P-recovery activation template

`recovery/OpenStepMGAG450Recovery.table` is the exact proposed Configure input
for the future P-recovery snapshot.  It matches the one observed PCI function,
uses the approved 16 MiB and 1600x1200@60 deployment record, and identifies
itself as `P-recovery`.

The file is intentionally outside every `.config` bundle and the staging
package builder explicitly does not include it.  It is therefore not
installable configuration and cannot create a candidate by itself.

Before the file can be used, a separately reviewed recovery-capable driver
bundle must replace the current R4 `+probe: NO` skeleton.  The actual run must
then use Installer/Configure to create a P-recovery snapshot with the original
Matrox instance disabled, reboot through the independent recovery channel, and
finally restore P-original as P-failure evidence.  No direct file copy or
runtime unload is permitted.

`tools/check-g450-recovery-activation-template.sh` checks the exact function,
mode and 16 MiB cap and also proves that the staging package does not leak this
matching template.
