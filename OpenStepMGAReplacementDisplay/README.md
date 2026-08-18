# OpenStepMGAReplacementDisplay — R4 staging skeleton

이 directory는 `IOFrameBufferDisplay` replacement driver의 source/build
skeleton이다. `MatroxMGA`가 화면을 소유한 현재 production configuration에는
설치하거나 등록하지 않는다.

`Default.table`에는 의도적으로 `Auto Detect IDs`, `Display Mode`, memory map,
I/O range가 없다. class의 `+probe:`도 항상 `NO`를 반환한다. 그러므로 이 tree를
build하는 일은 driver selection, device claim, framebuffer mapping, mode setting을
수행하지 않는다.

향후 변경 권한은 다음 문서의 G1~G6 gate를 따른다.

- `../docs/RECOVERY_REPLACEMENT_DRIVER_EXECUTION_PLAN.md`
- `../docs/D1_REPLACEMENT_DISPLAY_OWNERSHIP.md`
- `../docs/R4_SKELETON_REVIEW.md`

target build product는 source tree 안의 ignored
`OpenStepMGAReplacementDisplay.config/`에만 생성된다. `make install`,
`kl_util`, `driverLoader`, `/private/Drivers/i386` 복사는 R4 작업 범위 밖이다.
