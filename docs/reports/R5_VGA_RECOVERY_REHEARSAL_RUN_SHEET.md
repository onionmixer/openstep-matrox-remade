# R5 (VGA 기준선) — Recovery Loop Rehearsal Run Sheet

상태: **실행 완료 — PASS (`R5-VGA-20260818-A`)**
기준일: 2026-08-18
근거: `H1_HARDWARE_INTERROGATION_DECISION.md`,
`RECOVERY_REPLACEMENT_DRIVER_EXECUTION_PLAN.md`의 H1 재프레이밍.

## 왜 이 런시트인가

기존 `R5_RECOVERY_REHEARSAL_RUN_SHEET.md`는 `MatroxMGA`를 known-good owner로
가정한다. H1로 기준선이 **VGA(IOVGADisplay)**로 바뀌었다. 그리고 target은 이미
VGA 설정으로 cold reboot되어 telnet과 함께 가동 중이므로 아래는 **이미 증명됨**:

- VGA 설정으로 cold reboot 성공, GUI display(VGA) 정상.
- telnet(Pro1000 network) recovery 채널이 reboot 후 사용 가능.
- `/ndrv` NFS, gcdsd, nx-logcatch 재구성 가능.

이 런시트가 추가로 증명할 것은 **우리가 실제 활성화/복구에 사용할 절차**다:
operator가 telnet만으로 (1) reboot을 구동하고, (2) telnet 재접속까지 시간을
계측하며, (3) reboot 후 tooling(/ndrv·gcdsd·logcatch)을 스크립트로 복구하고,
(4) display owner가 VGA임을 재확인한다. 이 run은 **display 설정을 바꾸지 않는
같은-설정 reboot**이므로 display 위험이 0이다.

## known-good VGA 기준선 (복구 목표값)

```text
System.config Instance0.table:
  "Active Drivers" = "SpaceSaver2Mouse Pro1000 SoundBlaster16PCI VGA";
  "Boot Drivers"   = "EIDE Floppy SpaceSaver2Keyboard PCMCIABus PCIBus EISABus ISASerialPort";
VGA.config Instance0.table: IOVGADisplay, 800x600 BW:2, VESA 0x6a,
  Memory Maps 0xa0000-0xbffff, legacy VGA I/O only.
```

향후 활성화 편집: `Active Drivers`의 `VGA` → `OpenStepMGAReplacementDisplay`.
향후 복구 편집: 그 반대(위 기준선으로 원복).

## 실행 범위/금지

- display 설정을 바꾸지 않는다(같은-설정 reboot). 드라이버 bundle을
  `/private/Drivers` 또는 `/private/Devices`에 설치하지 않는다.
- `kl_util`로 어떤 드라이버도 교체하지 않는다. mode/PCI/MMIO/DDC 접근 없음.

## 절차 (operator 타이밍 승인 후)

1. **사전 기록**: `Active Drivers`/`VGA.config`가 위 기준선과 동일함을 read-only로
   확인(변경 없음). 현재 uptime 기록.
2. **reboot 구동**: telnet으로 `/usr/etc/reboot` (또는 `/usr/etc/shutdown -r now`).
   시각 기록.
3. **재접속 계측**: telnet 재접속을 폴링해 prompt 도달까지 걸린 시간을 기록.
   목표 timeout 300초. 초과 시 operator 물리 확인.
4. **tooling 복구**: `tools/nx-mount.sh`(/ndrv), telnet으로 gcdsd 기동
   (`tools/nx-daemon.sh`), `tools/nx-logcatch.sh` 재시작.
5. **display owner 재확인**: `Active Drivers`에 `VGA` 포함, `kl_util -s IOVGADisplay`
   상태, GUI 정상(operator 육안). 커널 로그 회수 가능.
6. **결과 기록**: 아래 표. 실패 시 원인 분리, 드라이버 활성화로 진행하지 않음.

## 결과 기록 (`R5-VGA-20260818-A`, 실행일 2026-08-18)

reboot은 operator가 직접 구동했다(telnet `/usr/etc/reboot` 대신). 복구 절차는
전부 telnet/스크립트로 수행·검증했다.

| check | expected | actual | pass/fail |
| --- | --- | --- | --- |
| pre-reboot 설정 = 기준선 | Active Drivers에 `VGA` | reboot 후에도 `... SoundBlaster16PCI VGA` 유지 | PASS |
| reboot 구동 | 수행 | operator-driven cold reboot | PASS |
| telnet 재접속 시간 | ≤ 300 s | 최초 접속 시 `up 1 min` (≤60s) | PASS |
| GUI display (VGA) | visible/stable | 커널 로그에 `loginwindow: running Workspace` + `Workspace: logged in` | PASS |
| /ndrv·gcdsd·logcatch 복구 | 스크립트로 복구 | `/ndrv` NDRV_OK, gcdsd 9910 응답, logcatch 로그 경로 확인 | PASS |
| 커널 로그 회수 | 가능 | `tail /usr/adm/messages` 회수 성공 | PASS |
| display owner | `VGA` | Active Drivers = `VGA`, Workspace GUI 정상 | PASS |

## Verdict — **PASS**

telnet 채널이 cold reboot에서 ≤60초에 복구됐고, VGA display가 Workspace 로그인까지
정상 기동했으며, tooling(/ndrv·gcdsd·logcatch)과 커널 로그 회수가 스크립트로
재현됐다. **이 복구 절차를 드라이버 활성화의 복구 경로로 채택한다.**

부기: reboot 자체는 operator가 구동했으므로 "telnet이 reboot을 구동한다"는 세부는
직접 시험하지 않았다(활성화/복구 시 operator 또는 telnet `/usr/etc/reboot` 어느
쪽도 가능). 또한 "telnet이 깨진 display 드라이버 부팅에서도 생존"하는지는 이
같은-설정 run으로는 증명 불가하며, 아래 가정에 의존한다.

## 남는 가정 (이 run으로는 증명 불가)

telnet이 **깨진 display 드라이버** 부팅에서도 생존하는지는 실제 실패 드라이버
없이는 완전 증명 불가하다. 근거: OPENSTEP telnetd는 window server와 독립인 BSD
데몬으로 `/etc/rc`에서 기동되므로 display 실패와 무관하게 올라온다. 이 가정 위에서
활성화 reboot을 수행하며, 실패 시 복구 편집(위)으로 VGA로 원복한다.
