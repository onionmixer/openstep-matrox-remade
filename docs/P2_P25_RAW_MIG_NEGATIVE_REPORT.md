# P2.5 — Raw MiG Negative-message Report

실행일: 2026-08-18

## 목적과 안전 범위

P2.5는 generated user stub을 우회한 malformed fixed-size request가 generated
server의 type/size validation에서 안전하게 거부되는지 확인한다. 이 단계는
fuzzing framework가 아니며 deterministic한 두 negative vector만 사용한다.

- 대상 routine: `query_capabilities` (MiG request ID `4101`)
- static query만 대상으로 하며 lease를 acquire하거나 release하지 않는다.
- OOL data, port descriptor, physical address, BAR/MMIO, DMA, IRQ, DDC, display
  API를 보내지 않는다.
- target MiG-generated `OpenStepMGAServer.c`의 `TypeCheck` code는 이 request가
  `msg_size=32`, `msg_simple=TRUE`, `MSG_TYPE_INTEGER_32/32-bit/count=1`
  descriptor여야 한다고 명시한다.

OPENSTEP 4.2의 generated client stub은 modern `mach_msg()`가 아닌 legacy
`msg_header_t`/`msg_rpc()` ABI를 사용한다. 따라서 test도 target-generated
source와 같은 ABI를 사용한다. reply port는 target에서 이미 검증된 legacy
`port_allocate()`로 만들고 test 종료 때 `port_deallocate()`한다.

## negative vectors

| vector | raw request | 기대 reply |
| --- | --- | --- |
| `SHORT` | header만 전송, `msg_size=sizeof(msg_header_t)=24` | reply ID `4201`, 32-byte death-pill, `MIG_BAD_ARGUMENTS (-304)` |
| `WRONG_TYPE` | 32-byte request이지만 scalar descriptor name을 `MSG_TYPE_INTEGER_16`으로 변경 | reply ID `4201`, 32-byte death-pill, `MIG_BAD_ARGUMENTS (-304)` |

두 message 모두 `msg_simple=TRUE`, `MSG_TYPE_NORMAL | MSG_TYPE_RPC`, 기존
advertised service port 및 test-owned reply port만 사용한다. 즉 Mach IPC
transport가 해석해야 하는 port right나 OOL descriptor 자체를 malformed하게
만들지 않는다.

## target-native build와 결과

target `/tmp/OpenStepMGAService/OpenStepMGAService_reloc.tproj`에서 generated
`OpenStepMGAUser.c`와 함께 native i386 client를 build했다.

```
OPENSTEP_MGA_P25_BUILD_STATUS=0,0
```

named temporary P2.3 service load 뒤 결과는 다음과 같다.

```
OPENSTEP_MGA_RAW_SHORT_TRANSPORT result=0 reply_id=4201 reply_size=32 reply_code=-304
OPENSTEP_MGA_RAW_WRONG_TYPE_TRANSPORT result=0 reply_id=4201 reply_size=32 reply_code=-304
OPENSTEP_MGA_RAW_RECOVERY result=0 features=0000001f max_leases=1 hardware_ready=0
OPENSTEP_MGA_P25_RAW_STATUS=0
```

두 negative vector의 `result=0`은 raw `msg_rpc()` transport가 성공했음을,
`reply_code=-304`는 generated server의 `MIG_BAD_ARGUMENTS` rejection을 뜻한다.
마지막 recovery RPC가 성공했으므로 malformed request 뒤에도 service의 normal
dispatch path와 explicit no-hardware capability state가 유지됨을 확인했다.

시험 뒤 temporary `OpenStepMGAService`만 unload/delete되어 `Deallocated`됐고,
기존 `MatroxMGA`는 계속 `Loaded at address 0x2112a000 for 0x12000 bytes`였다.

## 판정과 한계

P2.5는 raw short-message와 raw wrong-type message를 target에서 deterministic하게
거부·복구했다. 이는 unbounded random fuzzing, hostile Mach right/OOL descriptor
시험, user-space security boundary, GPU command processing 또는 hardware
ownership 검증을 의미하지 않는다.
