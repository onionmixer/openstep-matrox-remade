# X.Org MGA source-analysis notice

The project reviewed the official `xf86-video-mga-2.0.0` archive from the
X.Org driver archive on 2026-08-18. The reviewed tarball SHA-256 is:

```text
15b0f4cf3ee22eaefb45d54d1a0bf67ee710a292479a273fe3fd86f9fa802f41
```

Its `COPYING` begins with the XFree86 Project and Open Group permission
notices, permitting use, copy, modification, distribution and sale subject to
retaining the applicable copyright and permission notices. The archive is an
analysis input and is not vendored in this repository.

`OpenStepMGAG450PLLEncoding` and
`OpenStepMGAG450PrimaryCRTCImage` were independently written for this
project. No X.Org function, macro, register-write sequence, or source file is
copied into them. They retain only behavior-level, offline-reviewed byte
representations for one plan and perform no device I/O. If future code copies
or adapts any X.Org expression or source, it must add the complete applicable
notice before that change is accepted.
