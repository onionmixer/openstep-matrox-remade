# P2.2 — Client Port-Death Lease Recovery Report

실행일: 2026-08-18

## 범위와 ABI

P2.2는 hardware 접근 없이 P2.1 software lease의 abnormal-client recovery를
추가한다.

- protocol version: `2` (P2.1 version `1`과 ABI 호환되지 않음)
- `acquire(server, client_protocol, client_port, token, generation)`
- `client_port`: client가 legacy Mach `port_allocate()`로 만든 owner/control
  port. OPENSTEP의 `port_t` MiG type은 copy-send right로 전달된다.
- LKS: `kern_serv_notify()`로 copied send right를 감시하고, load command의
  `PORT_DEATH openStepMGAServicePortDeath` callback에서 matching lease를 해제한다.
- normal release: `kern_serv_port_gone()` 뒤 server copy를 `port_deallocate()`한다.
- 제외: MGA, PCI/BAR, VRAM/MMIO, DDC, DMA, IRQ, mode/DAC/PLL/CRTC.

## OPENSTEP 원전 근거와 compatibility 확인

NextDev LKS 문서는 `kern_serv_notify(ksp, reply_port, request_port)`,
`kern_serv_notify_port()`, `PORT_DEATH`, `kern_serv_port_gone()`을 제공한다.
`Designing`은 port-death function을 server가 send right를 가진 port가 죽을 때
호출되는 함수로 정의한다.

target user program에서 modern `mach_port_*` API는 headers의 defs에는 있어도
link library에 없었다. 반대로 legacy `port_allocate()`/`port_deallocate()`
probe는 exit 0으로 통과했다.

```
OPENSTEP_MGA_CLIENT_PORT result=0 port=2816
OPENSTEP_MGA_CLIENT_PORT_STATUS=0
```

kernel LKS에서 user `task_self_` macro를 사용한 최초 candidate는 `kl_util -a`
link 단계에서 실패했고 실행되지 않았다. OPENSTEP LKS용
`kern_serv_kernel_task_port()`로 수정한 clean build는 성공했다. 이는
kernel/user Mach ABI를 혼동하지 않기 위한 수정이며, display driver나 MGA
hardware에는 접근하지 않았다.

## build와 normal path

target `/tmp/OpenStepMGAService`에서 clean native build와 MiG regeneration을
완료했다. final relocatable size는 `81,808` bytes다. P2.2 normal smoke는 다음을
포함해 exit 0으로 통과했다.

```
OPENSTEP_MGA_PROTOCOL result=0 protocol=2 features=0000000f
OPENSTEP_MGA_PROTOCOL_BAD_VERSION result=4
OPENSTEP_MGA_ACQUIRE_BAD_VERSION result=4
OPENSTEP_MGA_ACQUIRE result=0 token=1 generation=1
OPENSTEP_MGA_RELEASE result=0
OPENSTEP_MGA_REACQUIRE result=0 token=2 generation=2
OPENSTEP_MGA_RERELEASE result=0
OPENSTEP_MGA_P22_NORMAL_STATUS=0
```

## client-death proof

temporary service load 뒤 first process가 control port를 포함한 lease를 잡고
`Release` 없이 종료했다. 1초 뒤 second process는 새 control port로 acquire와
release를 수행했다.

```
OPENSTEP_MGA_LEASE_ABANDON result=0 token=1 generation=1
OPENSTEP_MGA_LEASE_AFTER_CLIENT_EXIT result=0 token=2 generation=2
OPENSTEP_MGA_LEASE_AFTER_CLIENT_EXIT_RELEASE result=0
OPENSTEP_MGA_P22_CLIENT_DEATH_STATUS=0,0
```

NFS kernel log에는 다음 callback evidence가 있다.

```
OpenStepMGAService: P2.2 client port died; lease released
```

### 반복 release gate — 통과

같은 client control port로 sequential acquire/release를 반복하는 target-native
test도 통과했다.

```
OPENSTEP_MGA_LEASE_LOOP iterations=100  final_generation=100  result=0
OPENSTEP_MGA_P22_LOOP100_STATUS=0
OPENSTEP_MGA_LEASE_LOOP iterations=1000 final_generation=1000 result=0
OPENSTEP_MGA_P22_LOOP1000_STATUS=0
```

각 run 뒤 service는 named unload/delete됐고, `MatroxMGA`는 계속 `Loaded`였다.

### concurrent-owner gate — 통과

target shell에서 holder process가 4초 동안 lease를 유지하게 하고, 1초 뒤 별도
process가 acquire를 시도했다. holder의 normal release와 named cleanup까지
완료한 결과는 다음과 같다.

```
OPENSTEP_MGA_LEASE_AFTER_CLIENT_EXIT result=6 token=0 generation=0
OPENSTEP_MGA_LEASE_HOLD_ACQUIRE result=0 token=1 generation=1
OPENSTEP_MGA_LEASE_HOLD_RELEASE result=0
OPENSTEP_MGA_P22_CONCURRENT_STATUS=0
```

첫 label은 당시 P2.1 busy-probe의 historical 이름이며, source는 이후
`OPENSTEP_MGA_LEASE_BUSY_PROBE`로 정리했다. 값 `6`은
`KERN_RESOURCE_SHORTAGE`다. 즉 active owner가 있는 동안 별도 process는
lease를 받지 못했고 holder 종료 뒤 `MatroxMGA`는 계속 `Loaded` 상태였다.

시험 뒤 named `OpenStepMGAService`만 unload/delete되어 `Deallocated`됐고,
`MatroxMGA`는 `0x2112a000`, `0x12000 bytes`, `Loaded` 상태를 유지했다.

## 판정

P2.2는 single software lease의 normal release, stale/busy/version rejection,
client process exit 후 automatic lease recovery를 target에서 통과했다. 이는
GPU resource ownership, multi-client security, command submission, DMA 또는
3D acceleration의 통과가 아니다.
