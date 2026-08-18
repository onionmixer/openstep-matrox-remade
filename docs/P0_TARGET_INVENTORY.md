# P0.1 — Target Inventory

기준일: 2026-08-18

## 변경 없는 재확인

다음 명령은 read-only로 실행했다.

```
/usr/etc/kl_util -s MatroxMGA
cat /private/Drivers/i386/MatroxMGA.config/Instance0.table
ls -l /private/Drivers/i386/MatroxMGA.config/MatroxMGA{,_reloc}
```

결과:

- `MatroxMGA` kernel server는 `0x2112a000`, size `0x12000`으로 적재되어 있다.
- bundle의 executable은 1,068 bytes, preloaded relocatable은 104,788 bytes다.
- Instance table은 `MatroxMGAG400_16MB`, 1600x1200 @ 60Hz, RGB:888/32,
  `MGA Memory Size=16`, DAC 300MHz로 설정되어 있다. profile title은
  `Matrox MGA G400 (16MB or greater)`이므로 이 값은 최소 compatibility
  profile이지 물리 VRAM의 독립 측정값이 아니다.
- Instance table의 `IRQ Levels`는 비어 있다. 이는 PCI config의 IRQ 존재
  여부와는 별개의 DriverKit configuration 값이다.

이 작업은 display driver를 unload/reload하거나 mode를 바꾸지 않았다.

## PCI inventory

현재 부팅의 기준선은 P1 read-only probe가 2026-08-18에 기록한 값이다.
이 probe는 PCI configuration mechanism #1로 header만 읽었고, MGA BAR을
map하거나 MGA/VRAM/DAC/PLL/engine register를 읽거나 쓰지 않았다.

| 항목 | P1 실기 관찰값 | 상태 |
| --- | --- | --- |
| function | `04:00.0` | 확인됨 |
| vendor/device | `102b:0525` | 확인됨 |
| revision | `0x85` | 확인됨 |
| class | `030000` | 확인됨 |
| interrupt | IRQ 11, pin A | 확인됨 |
| command | `0x0007`: I/O, memory, bus master enabled | 확인됨 |
| BAR0 (raw) | `0xf8000008` | 확인됨; 주소는 `0xf8000000` |
| BAR1 (raw) | `0xe8200000` | 확인됨 |
| BAR2 (raw) | `0xe8800000` | 확인됨 |
| upstream bridge | `03:0d.0` HiNT HB4, `3388:0021` | 기존 PCI scan과 일치 |

P1 kernel log의 완전한 semantic record는 다음과 같다.

```
MGA-PROBE begin version=1 stage=0 read-only-pci-config
MGA-PROBE device 04:00.0 vid=102b did=0525 rev=85 class=030000
MGA-PROBE command=0007 status=0290 irq=11 pin=1
MGA-PROBE bars f8000008 e8200000 e8800000
MGA-PROBE end version=1
```

### 이전 기록과의 차이

2026-07-20의 `pcils` 기록은 BAR1을 `0xe8300000`으로 보고했다. 나머지
식별값과 topology는 P1과 일치하지만 BAR1은 1 MiB 다르다. `/tmp/pcils/pcils`는
재부팅 뒤 소실되어 동일 시점의 원자료를 다시 실행할 수 없다.

독립 `pcils/PCIscan`을 같은 부팅에서 rebuild하여 P1과 대조한 결과, 64-byte
header와 BAR0/1/2가 P1과 정확히 일치했다. 따라서 과거 scan은 역사 기록으로
보존하되, **현재 P2 이후의 주소 기준으로 사용해서는 안 된다.** 현재 부팅의
canonical 값은 P1/P1.1 공통값(`0xe8200000`)이다. BAR1 차이의 원인이 PCI
resource 재배치인지 과거 기록 오류인지는 확정하지 않는다. 상세 실행 기록은
`P1_PCIL_RECHECK.md`에 있다.

이 교차검증은 config inventory의 일치만 확정한다. 어느 BAR도 map하거나
참조할 수 있는 권한을 부여하지 않는다.

## PCI G450 판정 근거

FreeBSD stable/12 `mga_drv.c`는 `0525` function이 HiNT `3388:0021` bridge
뒤에 있을 때 PCI G450라고 처리한다. 실기의 bus topology가 일치하므로,
`MatroxMGAG400_16MB`라는 기존 table 이름보다 이 판정을 우선한다.

| 결론 | 상태 |
| --- | --- |
| 물리 카드 family | MGA G400/G450 compatible |
| 가속 구현 기준 | **PCI G450** |
| physical VRAM size/type | **미확정** — 16 MiB config와 `102b:0d43`의 32 Mb catalogue 표기가 충돌 |
| AGP/GART 사용 | 금지 |

## VRAM 안전 경계

1600x1200 32-bit display mode의 packed-pixel 최소치만 계산하면 7,680,000
bytes다. 그러나 OpenStep table의 16 MiB는 configuration 값이며, PCI subsystem
`102b:0d43`은 공개 catalogue에서 32 Mb board label로도 나타난다. 어느 값도
현재 실기의 물리 VRAM total을 독립적으로 증명하지 않는다. stride padding,
cursor, hidden display allocation, WindowServer 사용 영역도 미확정이다. 따라서
`16 MiB - 7,680,000` 또는 `32 MiB - 7,680,000` 계산값을 offscreen VRAM으로
사용해서는 안 된다. 대조 근거와 후속 gate는
`P1_SUBSYSTEM_MEMORY_RECONCILIATION.md`에 있다.

P2의 자원 소유권 판정 전에는 VRAM address에 대한 write와 GPU command
submission을 하지 않는다.

### P1.2 DriverKit runtime metadata query

documented user-space `IODeviceMaster` getter query는 current public display를
`Display0` (object `20`, `Linear Framebuffer`)로 보고했고, current mode index
`115`와 runtime mode count `212`를 반환했다. 그러나 `IOGetDisplayMemory`와
`IOGetRAMDACSpeed`는 transport success와 함께 `0`을 반환했다. 따라서
configuration table의 16MB/300MHz를 runtime getter로 독립 확인하지 못했으며,
그 `0`을 physical hardware 값으로 해석해서는 안 된다. 상세 evidence는
`P1_DRIVERKIT_DISPLAY_QUERY.md`에 있다.
