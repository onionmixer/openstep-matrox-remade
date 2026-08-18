# P1.1 — Independent PCI Configuration Recheck

기준일: 2026-08-18

## 목적

P1 `OpenStepMGAProbe`가 보고한 BAR1 `0xe8200000`과 2026-07-20 `pcils`
기록의 `0xe8300000` 차이를, P1 source와 독립된 기존 `pcils/PCIscan`으로
같은 부팅에서 확인한다.

## 사용한 도구와 안전 경계

`pcils`는 OPENSTEP i386에서 user-space IN/OUT이 허용되지 않는 제약 때문에,
`PCIscan` loadable kernel server가 PCI configuration mechanism #1
(`0xCF8`/`0xCFC`)로 64-byte header만 읽고 `IOLog`에 기록하는 구조다.

- source: workspace root `pcils/` (P1 probe와 별도 source tree)
- build: target `/tmp/OpenStepMGAPCIScan`
- installation: `/private/Devices`에 설치하지 않음
- hardware access: PCI config read만 수행; MGA BAR mapping, VRAM/MMIO/DAC/PLL/
  reset/DMA/IRQ/PCI config write는 하지 않음
- lifecycle: frontend가 `PCIscan`을 add → load → unload → delete. 실행 뒤
  `kl_util -s PCIscan`은 `Deallocated`를 보고함

NFS log capture를 먼저 시작했고, target에서 build/scan 전후 `sync; sync`를
수행했다.

## 현재 부팅 결과

`pcils -v`는 Matrox function에 아래 64-byte header를 기록했다.

```
04:00.0 VGA compatible controller [0300]: Matrox MGA G400/G450 [102b:0525] rev 85
    Subsystem: [102b:0d43] Matrox
    IRQ 11, pin A
    Command 0x0007, Status 0x0290
    BAR0: mem  0xf8000000 (32-bit, prefetchable)
    BAR1: mem  0xe8200000 (32-bit, non-prefetchable)
    BAR2: mem  0xe8800000 (32-bit, non-prefetchable)
    Config header:
      00: 0525102b 02900007 03000085 00008008
      10: f8000008 e8200000 e8800000 00000000
      20: 00000000 00000000 00000000 0d43102b
      30: 00000000 000000dc 00000000 2010010b
```

이는 P1 kernel log의 raw BAR record `f8000008 e8200000 e8800000`과 정확히
일치한다. 따라서 현 부팅의 canonical PCI snapshot은 P1/P1.1 공통값으로
고정한다. 과거 `e8300000`은 현재 하드웨어 접근 기준이 될 수 없으며,
그 차이의 역사적 원인은 확정하지 않는다.

## postcondition

- `MatroxMGA`는 scan 뒤에도 `0x2112a000`, `0x12000 bytes`로 `Loaded` 상태다.
- `PCIscan`은 `Deallocated` 상태다.
- kernel log에는 complete `PCIS BEGIN 1` → `PCIS END 11` block이 남았다.
- 화면 mode, framebuffer, Matrox register에는 어떤 변경도 하지 않았다.

## 판정

P2 control-plane implementation을 시작하기 위한 **PCI inventory 일치 gate는
통과**했다. 이것은 BAR mapping 또는 offscreen VRAM 사용 승인과 다르다.
그 자원 소유권 gate는 여전히 `P2_RESOURCE_OWNERSHIP.md`에 따라 미통과다.
