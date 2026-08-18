# R5 — Original-Driver Cold-Boot Recovery Result

상태: **PASS — original-driver cold-boot recovery reproduced**  
run ID: `R5-20260818-A`

## Scope and authority

The operator reported that OPENSTEP had been rebooted. This run retained the
P-original `MatroxMGA` snapshot. It did not install/load/probe the replacement
bundle, run Configure, edit an instance table, restart `driverLoader`, unload
`MatroxMGA`, or access PCI/MMIO/DDC.

The first post-reboot sandbox-local telnet attempts were refused. Retrying the
same read-only check outside the sandbox successfully reached `nextonion`; the
initial refusal is therefore treated as an execution-environment connectivity
limitation, not evidence that the target failed R5 recovery.

## Pre-reboot record

| check | observed result | verdict |
| --- | --- | --- |
| P-original snapshot | `MatroxMGA.config/Instance0.table`, no Configure/copy/edit action | pass |
| owner | `MatroxMGA` loaded | pass |
| mode | 1600×1200, 60 Hz, RGB:888/32 | pass |
| NFS | `/ndrv` mounted | pass |
| original fingerprint | four file comparator pass | pass |
| replacement production artifact | absent (prior R1 target evidence) | pass |

## Post-reboot record

| check | observed result | verdict |
| --- | --- | --- |
| independent remote recovery | outside-sandbox single-session telnet reached target | pass |
| loaded owner | `SERVER: MatroxMGA` | pass |
| configured table/mode | `MatroxMGAG400_16MB`; 1600×1200, 60 Hz, RGB:888/32 | pass |
| NFS source/log | `/ndrv` remounted with `hard,intr,timeo=30,retrans=5,rw` | pass |
| original fingerprint | all four files passed exact R0 comparator | pass |
| replacement production artifact | absent in `/private/Drivers/i386` and `/private/Devices` | pass |
| GUI display | operator confirmed the original OPENSTEP screen was visible and normal after reboot | pass |
| timeout/corruption | no corruption or instability reported in the recovery interval | pass |

## Verdict

`G4` is **PASS**. The original boot path, visible/stable GUI display,
outside-sandbox telnet recovery, NFS recovery, owner, configuration, original
artifact fingerprint, and replacement-artifact absence all passed after the
cold reboot.

This result validates only P-original recovery. It does not install, load,
probe, or authorize the replacement driver, and it does not pass G1, G2, G3,
or R6.
