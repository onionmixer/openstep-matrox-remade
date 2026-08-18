# P2.3 — Explicit Capability Contract Report

실행일: 2026-08-18

## 목적과 범위

P2.3은 기존 P2.2 client-death-cleanup software lease에, client가 현재
service가 하드웨어를 전혀 소유하거나 준비하지 않았음을 기계적으로 판별할 수
있는 `query_capabilities` MiG routine을 추가한다.

- protocol version: `3` (version `2`와 ABI 호환되지 않음)
- `query_capabilities(server, client_protocol, feature_flags, max_leases,
  hardware_ready)`
- static reply: `feature_flags=0x0000001f`, `max_leases=1`,
  `hardware_ready=0`
- `hardware_ready=0`은 P2 service가 MGA PCI function, BAR, VRAM, MMIO, DMA,
  IRQ, DAC/PLL/CRTC 또는 DDC GPIO를 소유·map·read·write하지 않았다는
  interface-level contract다.
- 이 flag는 future hardware enable 권한이나 resource ownership을 부여하지
  않는다. 그런 전이는 `P2_RESOURCE_OWNERSHIP.md`의 P3 이전 조건을 모두
  만족하고 별도 review를 통과한 뒤에만 가능하다.

## target-native build

NFS source를 `/tmp/OpenStepMGAService`로 새로 복사한 뒤 target의 native
`make clean; make`로 MiG user/server C를 재생성하고 i386 relocatable을
생성했다. 산출물 크기는 `83,364` bytes였다.

유일한 compiler diagnostic은 `-DKERNEL` 때문에 target의 user-mode Mach
precompiled header를 사용할 수 없다는 기존의 harmless warning이었다. build,
MiG generation 및 `kl_util -a` link는 모두 성공했다.

## service lifecycle와 smoke 결과

named temporary service만 unload/delete한 뒤 `/tmp`의 P2.3 relocatable을
load했다. 다음 target-native smoke가 exit 0으로 통과했다.

```
OPENSTEP_MGA_PROTOCOL result=0 protocol=3 features=0000001f
OPENSTEP_MGA_CAPABILITIES result=0 features=0000001f max_leases=1 hardware_ready=0
OPENSTEP_MGA_CAPABILITIES_BAD_VERSION result=4
OPENSTEP_MGA_PROTOCOL_BAD_VERSION result=4
OPENSTEP_MGA_ACQUIRE_BAD_VERSION result=4
OPENSTEP_MGA_ACQUIRE result=0 token=1 generation=1
OPENSTEP_MGA_ACQUIRE_BUSY result=6
OPENSTEP_MGA_RELEASE_STALE result=4
OPENSTEP_MGA_RELEASE result=0
OPENSTEP_MGA_REACQUIRE result=0 token=2 generation=2
OPENSTEP_MGA_RERELEASE result=0
OPENSTEP_MGA_P23_CAPABILITIES_STATUS=0
```

`result=4`는 malformed version/stale generation의 `KERN_INVALID_ARGUMENT`,
`result=6`은 active single lease 중 second acquire의
`KERN_RESOURCE_SHORTAGE`다. 즉 capability query 추가가 version rejection,
single-owner busy 처리, stale-release rejection, generation 증가 및 normal
release 경로를 바꾸지 않았음을 확인했다.

시험 종료 뒤 temporary `OpenStepMGAService`만 unload/delete되어
`Deallocated`됐고, existing `MatroxMGA`는 계속 `Loaded at address
0x2112a000 for 0x12000 bytes`였다.

target `nm` 결과에서 `IOPCI`, `IOFrameBuffer`, `_map`, `_unmap`, `_vm_`의
forbidden hardware symbol match는 없었다(`egrep` exit `1`). 이는 source
범위와 별도로 P2.3 relocatable이 PCI device claim 또는 DriverKit framebuffer/
VM mapping symbol을 link하지 않았음을 확인하는 보조 검사다.

## 판정

P2.3 control plane은 target에서 재현 가능하게 build/load/query/unload됐다.
이는 안전한 상태를 client에 명시할 뿐이며, GPU acceleration, command
submission, memory mapping, DDC, mode set 또는 display-driver replacement의
검증이나 구현을 의미하지 않는다.
