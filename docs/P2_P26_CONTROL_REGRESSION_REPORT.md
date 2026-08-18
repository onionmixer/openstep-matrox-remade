# P2.6 — Target-native Control-plane Regression Report

실행일: 2026-08-18

## 목적

P2.6은 P2.0~P2.5의 independent evidence를 대체하지 않는다. 같은 clean,
temporary service lifecycle에서 target-native test client를 다시 build하고
실행해 P2 control plane의 재현성을 확인하는 regression 단계다.

runner는 `test/run-p2-control-regression.csh`다.

```
csh -f /ndrv/openstep-matrox-remade/test/run-p2-control-regression.csh \
    /tmp/OpenStepMGAService
```

인자는 target에서 `make clean; make`를 마친 temporary service directory다.
runner는 exact-name `OpenStepMGAService`만 unload/delete/load하고, `/private/Devices`
설치 또는 MGA PCI/BAR/VRAM/MMIO/DMA/IRQ/DDC/display access를 수행하지 않는다.

## build

NFS source를 target `/tmp/OpenStepMGAService`로 새 복사한 뒤 native build를
수행했다. P2.3 relocatable size는 `83,364` bytes였다. `-DKERNEL`과 target
user-mode Mach precompiled header 조건 차이에 따른 기존 warning 외에 service
build 오류는 없었다.

runner가 generated `OpenStepMGAUser.c`와 함께 build한 client 결과:

```
OPENSTEP_MGA_P26_BUILD_openstep-mga-protocol-smoke=0
OPENSTEP_MGA_P26_BUILD_openstep-mga-lease-loop=0
OPENSTEP_MGA_P26_BUILD_openstep-mga-lease-abandon=0
OPENSTEP_MGA_P26_BUILD_openstep-mga-lease-recovery-probe=0
OPENSTEP_MGA_P26_BUILD_openstep-mga-raw-mig-negative=0
OPENSTEP_MGA_P26_BUILD_openstep-mga-lease-contend=0
```

`lease-abandon` compile의 unused helper warning은 intentional이다. process exit가
client control port death를 만들도록 port를 명시적으로 deallocate하지 않는다.

## target-native regression 결과

temporary service load, 모든 test, named cleanup은 exit 0으로 끝났다.

```
OPENSTEP_MGA_P26_SERVICE_LOAD=0
OPENSTEP_MGA_PROTOCOL result=0 protocol=3 features=0000001f
OPENSTEP_MGA_CAPABILITIES result=0 features=0000001f max_leases=1 hardware_ready=0
OPENSTEP_MGA_LEASE_LOOP iterations=1000 final_generation=1002 result=0
OPENSTEP_MGA_LEASE_ABANDON result=0 token=1003 generation=1003
OPENSTEP_MGA_LEASE_AFTER_CLIENT_EXIT result=0 token=1004 generation=1004
OPENSTEP_MGA_LEASE_AFTER_CLIENT_EXIT_RELEASE result=0
OPENSTEP_MGA_RAW_SHORT_TRANSPORT result=0 reply_id=4201 reply_size=32 reply_code=-304
OPENSTEP_MGA_RAW_WRONG_TYPE_TRANSPORT result=0 reply_id=4201 reply_size=32 reply_code=-304
OPENSTEP_MGA_RAW_RECOVERY result=0 features=0000001f max_leases=1 hardware_ready=0
OPENSTEP_MGA_P24_CONTEND_CHILD_STATUS=0,0
OPENSTEP_MGA_P24_CONTEND_LOG_STATUS=0,0
OPENSTEP_MGA_P26_SUITE_STATUS=0
OPENSTEP_MGA_P26_RUNNER_STATUS=0
```

contention clients는 각각 1,000 successful pair와 busy reply를 기록했다. 각
client의 final generation은 scheduling 순서에 따라 다르며, 이 run의 final
global generation은 `3004`였다. named cleanup 뒤 확인한 existing driver 상태는
다음과 같다.

```
STATUS: Loaded at address 0x2112a000 for 0x12000 bytes
```

## 판정

P2.6은 P2.3 ABI의 no-hardware service가 current target toolchain과 NFS source로
재현 build/run/cleanup됨을 확인한다. 이 통과는 P3 resource ownership gate를
열지 않으며, command submission과 MGA hardware access는 계속 disabled 상태다.
