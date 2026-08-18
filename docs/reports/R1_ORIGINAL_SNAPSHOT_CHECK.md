# R1 — P-original snapshot check

evidence ID: `R1-20260818-C`  
date: 2026-08-18

## Scope

`test/check-r1-original-snapshot-target.csh` was executed from the NFS source
on OPENSTEP. It reads only the active original `Instance0.table` and checks
that the replacement bundle is absent from the production driver directory.
It does not invoke Configure or driverLoader, install/load a bundle, alter a
table, or access PCI/MMIO/display hardware.

## Result

```text
OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=pass:default-table
OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=pass:driver-name
OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=pass:device-id
OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=pass:location
OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_FIELD=pass:display-mode
OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_REPLACEMENT=pass:production-absent
OPENSTEP_MGA_R1_ORIGINAL_SNAPSHOT_STATUS=pass
```

The fixed P-original record is `MatroxMGAG400_16MB` / `MatroxMGA` /
`0x0525102B` / `Dev:0 Func:0 Bus:4` / 1600×1200@60 RGB:888/32.

This is current P-original evidence only. It neither creates P-recovery or
P-failure evidence nor establishes Installer rollback or G1 sole-owner
configuration completion.
