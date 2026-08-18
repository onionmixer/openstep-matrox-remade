# P2 — MiG Control Protocol Specification

상태: **P2.3 explicit no-hardware capability contract, P2.4 multi-client lease stress, P2.5 raw negative-message handling, P2.6 target-native regression target-verified**

기준일: 2026-08-18

## 근거와 범위

이 명세는 `P0_OPENSTEP_DOCUMENTS.md`에 대조 기록한 NextDev LKS 문서,
`kernserv/kern_server.defs`, `kernserv/kern_server_types.h`, 그리고 공식
`ProAudioSpectrum16`의 `SMAP` load-command 사용을 따른다.

초기 protocol은 card register, BAR, VRAM, IRQ, DMA와 무관한 **control plane**다.
따라서 이 문서대로 만든 P2 skeleton은 `MatroxMGA` display driver를 claim하거나
MGA hardware를 map/read/write하지 않는다.

## interface 선택

`OpenStepMGAService`는 새 UNIX character device와 ioctl interface를 만들지
않는다. MiG-generated message-based LKS로 구현한다.

- `.defs`는 client API와 kernel server dispatch가 공유하는 유일한 ABI source다.
- `SMAP`을 사용한다. MiG server interface는 in/out message를 받으므로,
  return data가 있는 `QueryCapabilities`와 `Acquire`에 맞는다.
- `ADVERTISE`는 kern_loader가 receive port를 service에 전달할 수 있게 한다.
  service name은 구현 전에 target의 network-name namespace에서 충돌 검사를
  거친다.
- P2에는 interrupt handler가 없으므로 `WIRE`를 넣지 않는다. P3/P5에서 IRQ를
  도입할 때만 다시 검토한다.
- `START`도 넣지 않는다. 첫 request에서 load되는 message server가 P2의
  목적에 맞고, unload safety를 먼저 검증할 수 있다.

`subsystem` 번호도 source에 임의로 고정하지 않는다. target headers 및 설치된
third-party `.defs`의 사용 범위를 조사하고, 중복하지 않는 번호를 문서에
예약한 뒤에만 `.defs`를 작성한다. MiG의 기본 생성 file name은 input filename이
아니라 `subsystem` 이름을 따른다. 따라서 project build는 항상 `-user`,
`-server`, `-header` output option을 명시해 생성물 이름을 고정한다.

## P2 wire protocol

P2는 모든 request/reply를 fixed-size inline scalar로 제한한다. MiG의 기본
return type은 `kern_return_t`이고, 아래의 symbolic result를 client header에
정의한다. 표의 `P2.0` 표시가 없는 routine은 설계만 존재하며 아직 source에
넣지 않았다.

| routine | request | reply | P2 동작 |
| --- | --- | --- | --- |
| `protocol_info` | client protocol version | service version, feature bits | **P2.3 통과**; version과 feature bitmap만 report |
| `query_capabilities` | client protocol version | feature bits, maximum lease count, hardware-ready flag | **P2.3 통과**; `NO_SUBMIT`, `NO_MMIO`, `NO_DMA`, client-death cleanup 및 no-hardware state를 명시 |
| `acquire` | protocol version, client-owned control port | nonzero lease token, generation | **P2.2 통과**; owner가 없을 때만 발급하고 port death를 감시 |
| `wait_fence` | token, fence number, bounded timeout | completed fence number | P2에는 submitted fence가 없으므로 `0`만 성공 |
| `release` | token, generation | status | **P2.1 통과**; matching active lease만 해제 |
| `submit` | token, command version, inline byte count | status | 항상 `MGA_NOT_READY`; payload 없음 |

`QueryCapabilities`는 physical address, BAR value, VRAM range, revision-ROM
data를 공개하지 않는다. 그런 값은 P1 inventory와 후속 read-only gate가
확정한 뒤에도 kernel-private 상태로 남긴다.

## ownership state machine

```
UNINITIALIZED -> READY -> LEASED -> READY
                   |        |
                   +-> FAULTED <-+
```

- `READY`: service는 hardware resource를 갖지 않고 lease만 발급할 수 있다.
- `LEASED`: 정확히 하나의 `(token, generation)`만 유효하다. 두 번째
  `acquire`는 busy를 돌려준다.
- `FAULTED`: P2에서는 도달하지 않아야 한다. invalid protocol, stale token,
  timeout은 `FAULTED`가 아니라 해당 request만 거부한다. P3 이후 GPU timeout만
  `FAULTED` 전이를 만들 수 있다.
- `release`는 active token/generation을 정확히 일치시켜야 한다. mismatch는
  state를 바꾸지 않는다.

token은 capability secret 또는 보안 경계가 아니다. P2.2 이후의 `acquire`는
client가 `port_allocate()`로 만든 control port를 받는다. LKS는
`kern_serv_notify()`와 `PORT_DEATH` callback으로 그 copied send right의 death를
감시하며, 실제 target test에서 release 없이 종료한 client의 lease를 자동
해제했다. 정상 `release`는 `kern_serv_port_gone()` 뒤 copied send right를
deallocate한다. 이는 abnormal client 종료의 cleanup이지, 다중 user 보안 또는
권한 검증을 제공하는 것은 아니다.

## 금지된 message 형태

- user virtual address, physical address, BAR offset, register value를 argument로
  받지 않는다.
- OOL descriptor, pointer, VM map, shared memory handle을 P2에 넣지 않는다.
- raw MMIO mapping 또는 mmap/ioctl compatibility interface를 제공하지 않는다.
- PCI config write, mode switch, reset, engine command, DMA, IRQ enable request를
  정의하지 않는다.

P3 이후 large command buffer가 반드시 필요해져도 OOL data를 raw pointer로
읽지 않는다. NextDev Designing의 규칙대로 page-aligned memory와
`vm_write()`/`vm_read()` mapping을 별 test stage에서 검증한다. message-based
server에서 `copyin()`/`copyout()`을 쓰지 않는다.

## build and target gates

1. target `/usr/bin/mig`가 존재함을 확인했다. 이 wrapper는 target architecture
   C preprocessor와 `/usr/lib/migcom`을 호출한다.
2. 2026-08-18 target `/tmp/OpenStepMGAMIGSmoke`에서 installed
   `kern_server.defs`를 input으로 non-advertised generation을 시험했다.
   default generation은 `kern_serv.h`, `kern_servUser.c`, `kern_servServer.c`를
   만들었고 status 0이었다. 이어 `-user SmokeUser.c -server SmokeServer.c
   -header Smoke.h`를 명시한 generation도 status 0이었다. 이 시험은 source
   generation만 하며 LKS나 MGA hardware를 load하지 않았다.
3. `.defs`는 source control에 넣고 generated C/header는 target build의 derived
   output으로 둔다. Makefile은 `/usr/bin/mig -user/-server/-header`를 명시해
   매 build에서 같은 이름으로 생성한다. 2026-08-18 P2.0 build가 이 규칙을
   통과했다.
4. target build, `kl_util` load, advertised port lookup, `protocol_info`,
   `query_capabilities`,
   software `Acquire`/`Release`, client-death cleanup 및 unload/delete cycle은
   통과했다. `P2_P20_PROTOCOL_REPORT.md`, `P2_P21_LEASE_REPORT.md`,
   `P2_P22_PORT_DEATH_REPORT.md`, `P2_P23_CAPABILITIES_REPORT.md`가 근거다.
5. `Acquire`/`Release` 100회 및 1,000회 반복, malformed version request,
   unload/reload/delete, client-death cleanup, one active holder와 one separate
   busy client의 concurrent-owner test는 통과했다. 둘 이상의 retrying client를
   동시 실행하는 multi-holder stress도 통과했다. 실행 증거는
   `P2_P24_MULTI_CLIENT_STRESS_REPORT.md`다. P2.5 fixed-size/type raw negative
   test도 통과했으며, 실행 증거는 `P2_P25_RAW_MIG_NEGATIVE_REPORT.md`다.
   unbounded random fuzzing 및 hostile port-right/OOL descriptor 시험은 P2
   범위 밖이며, 소유권·recovery 설계가 별도로 준비된 뒤에만 검토한다.
   multi-holder test source는
   `test/openstep-mga-lease-contend.c`와
   `test/run-p2-lease-contend.sh`다.

이 gate들이 통과해도 P2는 hardware access 권한을 얻지 않는다. BAR mismatch와
`P2_RESOURCE_OWNERSHIP.md`의 offscreen ownership gate는 독립적으로 남는다.

## 재현 regression runner

`test/run-p2-control-regression.csh`는 target-native로 빌드한 temporary
`/tmp/OpenStepMGAService`를 인자로 받아 P2.3 capability/smoke, 1,000회
sequential lease, client-death recovery, P2.5 raw negative message, P2.4
two-client contention을 한 lifecycle에서 build/run/cleanup한다. 기존
`MatroxMGA`가 마지막에 `Loaded`인지도 확인한다.

이 runner는 정확히 named `OpenStepMGAService`만 load/unload/delete하며
`/private/Devices`에 설치하지 않는다. P2 protocol 자체와 마찬가지로 MGA
PCI/BAR/VRAM/MMIO/DMA/IRQ/DDC/display API를 사용하지 않는다.

2026-08-18의 complete target result는 `P2_P26_CONTROL_REGRESSION_REPORT.md`에
기록한다.
