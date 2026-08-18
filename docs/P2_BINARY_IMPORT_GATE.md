# P2.8 — Target-native Binary Import Gate

기준일: 2026-08-18

## 목적

P2.7 source guard는 source/table/load command를 검사한다. P2.8은 OPENSTEP
toolchain이 실제로 만든 `OpenStepMGAService_reloc`의 undefined import table을
`/bin/nm -u`로 검사한다. 따라서 source include나 linker 변화로 physical mapping,
PCI ownership, framebuffer lifecycle, DMA/direct I/O helper가 binary에 유입되는
경우를 LKS load 전에 거부한다.

이 검사는 binary의 import name만 검사한다. indirect code path, target ownership,
VRAM layout, P3 admission을 증명하지 않는다.

## target baseline

2026-08-18, loaded되지 않은 `/tmp/OpenStepMGAService` build의 `nm -u`는 다음
generic support import만 보였다.

```
.objc_class_name_IODevice
.objc_class_name_Object
_IOLog
_kern_serv_kernel_task_port
_kern_serv_notify
_kern_serv_notify_port
_kern_serv_port_gone
_port_deallocate_EXTERNAL
```

`IOMapPhysicalIntoIOTask`, `IOUnmapPhysicalFromIOTask`,
`IOPhysicalFromVirtual`, `IOPCIDirectDevice`, `IODirectDevice`,
`IOFrameBufferDisplay`, PCI configuration helper, port-I/O helper, MGA PCI/MMIO
helper는 없었다. 이 read-only inspection 동안 `MatroxMGA`는 loaded 상태였고
`OpenStepMGAService`는 deallocated 상태였다.

P2.8은 위 baseline의 여덟 import만 허용하도록 이후 강화됐다. denylist에 아직
없는 새 import도 P2에 적합한지 검토가 필요하므로, unexpected import는 hardware
여부와 관계없이 service load 전에 실패한다.

## 실행

target에서 이미 build된 relocatable을 인수로 준다.

```
csh -f /ndrv/openstep-matrox-remade/test/check-p2-binary-imports.csh \
    /tmp/OpenStepMGAService/OpenStepMGAService.config/OpenStepMGAService_reloc
```

성공 시 결과는 다음과 같다.

```
OPENSTEP_MGA_P28_BINARY_GUARD_STATUS=pass
```

P2 control-plane regression runner는 service load 전에 이 검사를 실행한다.
실패하면 service를 load하지 않고 종료한다.

## target-native 통합 결과

2026-08-18에 다음 runner를 target에서 실행했다.

```
csh -f /ndrv/openstep-matrox-remade/test/run-p2-control-regression.csh \
    /tmp/OpenStepMGAService
```

관련 결과는 다음과 같다.

```
OPENSTEP_MGA_P28_BINARY_GUARD_STATUS=pass
OPENSTEP_MGA_P28_BINARY_GUARD=pass
OPENSTEP_MGA_P26_SERVICE_LOAD=0
OPENSTEP_MGA_P26_SUITE_STATUS=0
```

runner는 test 종료 뒤 `OpenStepMGAService`를 unload/delete했고,
`MatroxMGA`의 `STATUS: Loaded at address 0x2112a000 for 0x12000 bytes`를
확인했다. P2.8은 binary import와 P2 no-hardware lifecycle이 함께 유지됨을
보이지만, physical resource ownership 또는 P3 admission을 통과시킨 것은 아니다.

strict allowlist 강화 뒤에도 P2.9 clean build에서
`OPENSTEP_MGA_P28_BINARY_GUARD_STATUS=pass`와 full P2 suite success를 확인했다.
그 clean-run evidence는 `docs/P2_CLEAN_REPRODUCIBILITY.md`에 기록한다.
