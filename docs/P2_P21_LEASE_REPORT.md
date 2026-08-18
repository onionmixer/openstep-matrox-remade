# P2.1 — Software Lease MiG Service Report

실행일: 2026-08-18

## 범위

P2.1은 P2.0의 message-only service에 hardware와 무관한 단일 software lease를
추가한다.

- 구현 routine: `OSMGA_protocol_info`, `OSMGA_acquire`, `OSMGA_release`
- lease state: active token 하나와 monotonic generation 하나
- 제외: MGA device class, PCI/BAR, VRAM/MMIO, DMA, IRQ, DDC, mode/DAC/PLL/CRTC
- 설치: 없음. target `/tmp/OpenStepMGAService`에서만 native build 및 temporary
  `kl_util -a` load를 수행했다.

## target-native build

target `/tmp/OpenStepMGAService`에서 `/usr/bin/mig`와 i386 kernel compiler로
build했다.

- generated output: `OpenStepMGAUser.h`, `OpenStepMGAUser.c`,
  `OpenStepMGAServer.c`
- relocatable size: `77,576` bytes
- test client: `/tmp/openstep-mga-protocol-smoke`, `32,680` bytes

kernel compiler의 precompiled `mach.p` warning은 user-header cache와 `-DKERNEL`
macro가 일치하지 않는다는 것이며, MiG generation, server compile, link는 모두
성공했다.

## one-cycle lease proof

NFS kernel log capture 하에서 exact name `OpenStepMGAService`만 stale unload/
delete한 뒤 temporary relocatable을 allocate했다. advertised `openstepmga0`
port를 lookup한 target-native client의 결과는 다음과 같다.

```
OPENSTEP_MGA_PROTOCOL result=0 protocol=1 features=0000000f
OPENSTEP_MGA_ACQUIRE result=0 token=1 generation=1
OPENSTEP_MGA_ACQUIRE_BUSY result=6
OPENSTEP_MGA_RELEASE_STALE result=4
OPENSTEP_MGA_RELEASE result=0
OPENSTEP_MGA_REACQUIRE result=0 token=2 generation=2
OPENSTEP_MGA_RERELEASE result=0
```

`6`은 `KERN_RESOURCE_SHORTAGE`, `4`는 `KERN_INVALID_ARGUMENT`이다. 따라서
second acquire는 active lease를 유지한 채 거부됐고, stale generation release도
state를 변경하지 않았다. 정상 release 후에는 새 token과 새 generation을 받았다.

loaded 상태에서 service는 address `0x223b0000`, size `0x2000`, advertised port
`openstepmga0`으로 확인됐다. 시험 뒤 `OpenStepMGAService`만 unload/delete되어
`Deallocated`됐으며, 기존 `MatroxMGA`는 전후 모두 address `0x2112a000`, size
`0x12000`, `Loaded` 상태였다.

kernel log는 아래 순서를 기록했다.

```
OpenStepMGAService: P2.1 software lease initialized
OpenStepMGAService: P2.1 protocol_info
OpenStepMGAService: P2.1 lease acquired
OpenStepMGAService: P2.1 lease released
OpenStepMGAService: P2.1 lease acquired
OpenStepMGAService: P2.1 lease released
```

## 남은 P2 gate

### 반복 lease gate — 통과

별도 target-native `openstep-mga-lease-loop` client를 추가했다. 이 client는
단일 advertised port에 대해 순차 `Acquire → Release`만 반복하며, 매번 nonzero
token 및 이전과 다른 generation을 요구한다. hardware state를 request에 넣지
않는다.

temporary load/unload/delete를 각각 포함한 결과는 다음과 같다.

```
OPENSTEP_MGA_LEASE_LOOP iterations=100  final_generation=100  result=0
OPENSTEP_MGA_LEASE_LOOP iterations=1000 final_generation=1000 result=0
```

두 run 뒤 모두 `OpenStepMGAService`는 `Deallocated`됐고, `MatroxMGA`는
`0x2112a000`, `0x12000 bytes`, `Loaded` 상태였다. 이로써 P2의 1,000회 순차
software lease gate는 통과했다.

### 남은 P2 gate

### malformed scalar gate — 통과

`openstep-mga-protocol-smoke`는 정상 query 전에 wrong client protocol version을
각각 `protocol_info`와 `acquire`에 보냈다.

```
OPENSTEP_MGA_PROTOCOL_BAD_VERSION result=4
OPENSTEP_MGA_ACQUIRE_BAD_VERSION result=4
```

둘 다 `KERN_INVALID_ARGUMENT`으로 거부됐고, 같은 service instance에서 이어진
정상 acquire/release smoke는 exit 0으로 통과했다. raw MiG request는 당시에는
client stub 계약을 우회해야 하므로 이 단계에서 만들지 않았다. 이 제한은
P2.5의 deterministic fixed-size/type negative test로 supersede됐으며,
`P2_P25_RAW_MIG_NEGATIVE_REPORT.md`를 따른다.

### client-death 관찰 — P2.1의 historical baseline

별도 `openstep-mga-lease-abandon` process가 lease를 잡은 뒤 `Release` 없이
종료했고, 새 `openstep-mga-lease-busy-probe` process가 다음 결과를 보였다.

```
OPENSTEP_MGA_LEASE_ABANDON result=0 token=1 generation=1
OPENSTEP_MGA_LEASE_AFTER_CLIENT_EXIT result=6 token=0 generation=0
```

P2.1 당시에는 client 종료를 감지해 lease를 자동 해제하지 않았다. 이는
`P2_PROTOCOL.md`의 "single trusted local process만 지원, port-death cleanup은
미구현"과 일치하는 의도된 관찰 결과다. 시험은 바로 named unload/delete를
수행했고, 재적재 뒤 전체 smoke가 다시 exit 0으로 통과했다.

```
OPENSTEP_MGA_CLIENT_EXIT_STATUS=0,0,0
```

이 관찰은 P2.2에서 supersede됐다. P2.2는 Mach notification API를 원전으로
검증한 뒤 client control port를 explicit protocol argument로 추가했고,
release 없이 종료한 client의 lease 자동 해제를 target에서 통과했다. 현재
결과는 `P2_P22_PORT_DEATH_REPORT.md`를 기준으로 한다. 모든 P2 gate는
hardware resource ownership을 부여하지 않는다.
