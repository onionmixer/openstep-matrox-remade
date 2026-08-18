# P2.4 — Multi-client Software-Lease Stress Report

실행일: 2026-08-18

## 범위

P2.4는 P2.3 protocol ABI를 바꾸지 않는다. 이미 target-verified인 single
software lease가 실제로 동시에 실행되는 독립 user process 사이에서도
single-owner 규칙을 유지하는지 검증하는 control-plane hardening 단계다.

`test/openstep-mga-lease-contend.c`는 client control port 하나를 만들고,
요청 횟수만큼 acquire/release pair를 성공할 때까지 반복한다.

- `KERN_RESOURCE_SHORTAGE`는 다른 client가 lease를 보유한 정상 busy reply로
  세고 재시도한다.
- success token/generation이 nonzero여야 하며, 같은 client의 generation은
  이전 success와 달라야 한다.
- acquire/release 이외의 request, MGA register, PCI/BAR, VRAM/MMIO, DMA, IRQ,
  DDC 또는 display API는 사용하지 않는다.

`test/run-p2-lease-contend.sh`는 OPENSTEP에 설치된 `/bin/sh`의 independent
background-job 및 `wait` semantics로 두 client를 동시에 시작한다. 각 child
exit status와 각 log의 `result=0` line을 별도로 검증한다. interactive target
shell인 csh의 한 줄 background-list parsing에 test lifecycle가 합쳐지지 않게
하기 위해 runner 내부에는 POSIX shell job control을 사용했다.

## target-native 결과

P2.3 relocatable을 named temporary `OpenStepMGAService`로 load한 뒤, target
native i386 client 두 개를 각각 `1000` successful acquire/release pair로
실행했다.

```
OPENSTEP_MGA_LEASE_CONTEND successes=1000 busy=996 attempts=1996 final_generation=2000 result=0
OPENSTEP_MGA_LEASE_CONTEND successes=1000 busy=997 attempts=1997 final_generation=1997 result=0
OPENSTEP_MGA_P24_CONTEND_CHILD_STATUS=0,0
OPENSTEP_MGA_P24_CONTEND_LOG_STATUS=0,0
OPENSTEP_MGA_P24_CONTEND_STATUS=0
```

두 client가 받은 busy reply는 합계 `1,993`회다. 두 client의 success는 합계
`2,000`회이며, service를 fresh load한 run에서 관찰된 largest generation은
`2,000`이다. 개별 client 출력의 final generation은 process scheduling 순서에
따라 다를 수 있으므로 서로 크기를 비교하지 않는다.

시험 뒤 temporary `OpenStepMGAService`만 unload/delete되어 `Deallocated`됐다.
기존 display driver는 계속 다음 상태였다.

```
SERVER: MatroxMGA
STATUS: Loaded at address 0x2112a000 for 0x12000 bytes
```

## 판정

P2.3 software lease는 two-client contention에서 busy reply와 정상 release를
반복 처리했으며 leaked active owner 없이 cleanup됐다. 이는 user-space security
boundary, raw malformed MiG message fuzzing, GPU resource ownership, command
submission 또는 어떠한 MGA hardware access의 검증도 아니다.
