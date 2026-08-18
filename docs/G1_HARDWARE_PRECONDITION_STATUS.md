# G1 — Hardware-precondition status

기준일: 2026-08-18

운영자 지시에 따라 G1 작업 중 하드웨어 파악에 해당하는 전제는 완료로
처리한다. 이 결정의 deployment input은 다음으로 고정한다.

| item | completed decision |
| --- | --- |
| adapter family / topology | PCI Matrox G450, primary head only |
| deployment VRAM bound | conservative fixed 16 MiB |
| first display record | 1600×1200@60, RGB:888/32, 6400-byte pitch |
| clock/image input | 162 MHz DMT timing; reviewed primary CRTC and PLL byte images |
| excluded scope | dual head, automatic VRAM expansion, live VRAM probing, DDC/EDID, engine access |

This closes the hardware-understanding portion used by the offline G1/G3/R6
policies. It does **not** assert a physical board maximum, publish/map a
framebuffer range, program a register, or establish a replacement display
owner.

The remaining G1 item is configuration/recovery evidence: real P-recovery and
P-failure Configure snapshots, one-owner candidate verification, and
Installer rollback. Those are configuration operations rather than hardware
identification, and remain separately required before any recovery-only
hardware activation.
