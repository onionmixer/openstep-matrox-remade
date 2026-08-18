# R1 — G450 P-recovery configuration admission

`profile/OpenStepMGAG450RecoveryConfig.{h,c}` validates extracted table values
for the one reviewed recovery profile before a future bundle is allowed to
treat them as its G1 deployment input.

| field | required value |
| --- | --- |
| Driver Name | `OpenStepMGAReplacementDisplay` |
| Location | `Dev:0 Func:0 Bus:4` |
| Auto Detect IDs | `0x0525102B` |
| Display Mode | 1600×1200@60 RGB:888/32 |
| MGA Memory Size | `16` |
| Recovery Profile | `P-recovery` |

The validator accepts extracted strings only. It does not read a configuration
table, call Configure/driverLoader, install or load a bundle, claim PCI, map
memory, or access display hardware. A different PCI function, 32 MiB input,
or non-recovery profile fails closed.

```text
sh tools/check-g450-recovery-config-no-hardware.sh
sh test/run-g450-recovery-config-host.sh
# target: csh -f test/run-g450-recovery-config-target.csh /ndrv/openstep-matrox-remade
```

Host strict-C89 and OPENSTEP i386 target tests passed on 2026-08-18. This
establishes only configuration-value portability; it is not P-recovery
evidence and does not complete G1 sole-owner configuration.

The review-only source template itself was also checked read-only on OPENSTEP
NFS source with `test/verify-g450-recovery-template-target.csh` and returned
`OPENSTEP_MGA_G450_RECOVERY_TEMPLATE_TARGET_STATUS=pass`.
