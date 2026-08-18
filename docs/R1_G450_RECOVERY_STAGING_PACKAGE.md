# R1 — G450 recovery staging package

`packaging/openstep/build-recovery-staging-package.csh` creates an i386-only
OPENSTEP Installer package solely for reviewing the already fail-closed R4
bundle.  Its payload is deliberately rooted at:

```text
<selected prefix>/DriverStaging/OpenStepMGAReplacementDisplay.config
```

It never places a bundle at the production driver root, contains no `Auto
Detect IDs` entry, and the compiled class still returns `NO` from `+probe:`.
The package pre-install hook rejects all `/private...` destinations and non-i386
systems.  Consequently package installation alone cannot become a display
candidate or change the current `MatroxMGA` owner.

The build and verify scripts use only `/tmp/OpenStepMGARecoveryStage` plus the
source root.  Verification unpacks the generated archive, confirms its staging
path, absence of matching configuration, i386 relocatable, and i386-only BOM.
No Installer installation or Configure action is part of the scripts.

This completes the artifact/package-construction half of G1; it is **not**
Installer atomic-install evidence.  The remaining G1 evidence is an
operator-approved Installer/Configure run that creates a real P-recovery
instance with the original instance disabled, verifies installer rollback,
and then performs a P-failure original restoration.  Those actions change
boot configuration and are therefore not performed by a build script.

2026-08-18 target verification passed archive construction, payload/BOM review,
the safe `/LocalDeveloper` pre-install result, the rejected `/private/Drivers`
pre-install result, and the fail-closed relocatable import allowlist.  No
package was installed and no Configure state changed.
