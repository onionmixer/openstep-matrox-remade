# P2.9 — Clean-build Reproducibility Runner

기준일: 2026-08-18

## 목적

`test/run-p2-clean-regression.csh`는 NFS source tree의
`OpenStepMGAService`만 정확히 `/tmp/OpenStepMGAService-P29`에 복사하고,
복사 전후 `sync; sync` 뒤 `make clean; make`와 P2 control-plane runner를 수행한다.
성공·실패 어느 경우에도
그 exact temporary directory만 삭제한다.

이 runner는 `/private/Devices`에 설치하지 않으며, MGA BAR/MMIO/VRAM/DMA/IRQ/DDC,
display mode에 접근하지 않는다. P2.8 binary import gate는 LKS load 전에 실행된다.

## 실행 방법

```
csh -f /ndrv/openstep-matrox-remade/test/run-p2-clean-regression.csh \
    /ndrv/openstep-matrox-remade
```

성공 기준은 다음 두 marker다.

```
OPENSTEP_MGA_P29_CLEAN_BUILD=pass
OPENSTEP_MGA_P29_CLEAN_SUITE=pass
```

## 현재 검증 상태

2026-08-18 target에서 P2.9 runner를 한 번 실행해 다음 결과를 얻었다.

```
OPENSTEP_MGA_P29_CLEAN_BUILD=pass
OPENSTEP_MGA_P28_BINARY_GUARD_STATUS=pass
OPENSTEP_MGA_P28_BINARY_GUARD=pass
OPENSTEP_MGA_P26_SERVICE_LOAD=0
OPENSTEP_MGA_P26_SUITE_STATUS=0
OPENSTEP_MGA_P29_CLEAN_SUITE=pass
```

build 중 `mach.p` precompiled-header configuration warning은 target compiler가
`KERNEL` define으로 다시 compile했음을 알리는 기존 diagnostic이며 build는 성공했다.
runner는 종료 시 sidecar를 deallocate했고, `MatroxMGA`가
`0x2112a000`에서 `0x12000` bytes로 loaded 상태임을 확인했다.

P2 client-port helper의 test-only unused-function cleanup 뒤에도 같은 clean
runner를 다시 통과했다. 이 재실행에서는 이전의
`OSMGADeallocateClientPort defined but not used` warning도 나타나지 않았다.
