# P2.0 — Message-only MiG Service Report

실행일: 2026-08-18

## 범위

P2.0은 `OpenStepMGAService`의 MiG control plane만 검증한다.

- MiG subsystem: `openstep_mga 4100`
- advertised port: `openstepmga0`
- implemented routine: `OSMGA_protocol_info`
- request/reply: fixed-size integer scalar만 사용
- 제외: `Acquire`, `Release`, `Submit`, BAR mapping, VRAM/MMIO, PCI config,
  DMA, IRQ, mode/DAC/PLL/CRTC access

## 빌드

target `/tmp/OpenStepMGAService`에서 native `make`가 성공했다.

- `OpenStepMGA.defs`는 target `/usr/bin/mig`가 explicit output option으로
  `OpenStepMGAUser.h`, `OpenStepMGAUser.c`, `OpenStepMGAServer.c`를 생성했다.
- generated server dispatcher는 `openstep_mga_server`, routine implementation
  symbol은 `OSMGA_protocol_info`임을 target 생성물로 확인했다.
- `OpenStepMGAService_reloc` size: 72,336 bytes.
- bundle은 `/private/Devices`에 설치하지 않았다.

## Name-service preflight

`test/openstep-mga-namecheck.c`는 target에서 다음을 출력하고 exit 0으로
종료했다.

```
OPENSTEP_MGA_NAMECHECK name=openstepmga0 result=1001 port=0
```

`1001`은 `NETNAME_NOT_CHECKED_IN`이다. 이 client는 lookup만 수행했으며 port
생성/등록/해제를 하지 않았다.

## load / RPC / cleanup

NFS kernel log 수집을 시작한 뒤, 아래 한 사이클을 실행했다.

1. stale `OpenStepMGAService`가 있으면 unload/delete.
2. target `/tmp`의 relocatable을 `kl_util -a`로 allocate.
3. target-native smoke client가 `netname_look_up()`으로 `openstepmga0`을
   lookup. 이 lookup이 advertised port를 통해 service를 지연 load.
4. `OSMGA_protocol_info()` 호출.
5. status 확인 후 `kl_util -u`, `kl_util -d`.

RPC 결과:

```
OPENSTEP_MGA_PROTOCOL result=0 protocol=1 features=0000000f
```

`0x0000000f`는 `PROTOCOL_ONLY | NO_SUBMIT | NO_MMIO | NO_DMA`다. loaded 상태의
`kl_util -s`는 `openstepmga0(advertised)` port와 service address
`0x223b0000` size `0x2000`를 보고했다. cleanup 뒤 service는 `Deallocated`였다.

kernel log에는 다음 record가 남았다.

```
OpenStepMGAService: P2.0 protocol_info
```

## 안전 postcondition

- `MatroxMGA`는 시험 뒤에도 `0x2112a000`, `0x12000 bytes`, `Loaded` 상태다.
- service는 target `/tmp`에만 존재하며 driverLoader automatic registration은
  하지 않았다.
- source 및 execution path에는 MGA register/BAR/VRAM/PCI write가 없다.

따라서 P2.0의 MiG build, advertised port, lazy load, scalar RPC, unload/delete
cycle은 통과했다. 이는 하드웨어 가속 또는 card resource ownership의 통과가
아니다.
