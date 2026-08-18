# R2 — PCI Subsystem Candidate Profile

기준일: 2026-08-18

## candidate 결론

target의 observed PCI tuple `102b:0525`, subsystem vendor/device `102b:0d43`은
공개 hardware archive에서 Matrox `G45FMDVP32DSF`로 연결된다. 그 archive는 이
tuple에 대해 MGA-G450, 32 MB DDR, VGA+DVI-I connector를 기록한다.

동일 part number를 독립적으로 다루는 product records도 PCI 32-bit, 32 MB
graphics memory, Matrox G450을 일관되게 제시한다. 따라서 이 tuple의
**working candidate**는 다음과 같다.

| field | candidate value | evidence class |
| --- | --- | --- |
| product candidate | `G45FMDVP32DSF` | same PCI tuple hardware archive |
| GPU family | Matrox G450 | target PCI device + archive/product records |
| memory total | 32 MB | archive + independent product records |
| memory type | DDR | archive + part-number product records |
| bus | PCI, 32-bit | target topology + product records |
| connector shape | VGA + DVI-I, dual-head | archive/product records |

## sources and independence

- [PCI hardware archive](https://www.pc-schnulli.de/hardw/gkpci/magkpci.html)는
  `102b:0525`, subsystem vendor `102b`, subsystem `0d43`을
  `G45FMDVP32DSF`, 32 MB DDR, VGA/DVI-I로 기록한다.
- [B&H part-number specification](https://www.bhphotovideo.com/c/product/819813-REG/Matrox_G45FMDVP32DSF_G450_PCI_PCI_X_4_X.html)은
  같은 `G45FMDVP32DSF`에 PCI 32-bit 및 32 MB graphics memory를 기록한다.
- [Best Buy part-number specification](https://www.bestbuy.com/site/matrox-g450-graphic-card-32-mb-ddr-sdram-pci/4314186.p?skuId=4314186)는
  같은 model에 32 MB DDR SDRAM PCI와 Matrox G450을 기록한다.
- immutable `pci.ids` family catalogue도 `102b:0d43`을 32 Mb Dual Head PCI로
  표기한다. 이 자료는 part number를 제공하지 않으므로 primary identity source가
  아니라 consistency check로만 사용한다.

## G2 verdict: still pending

이 candidate는 기존 16 MiB OPENSTEP configuration profile보다 훨씬 강한
board-level hypothesis다. 그러나 PCI subsystem ID는 board sticker나 memory
population을 직접 읽은 값이 아니며, archive/product records는 target의
physical card가 해당 P/N임을 직접 증명하지 않는다.

따라서 아래 값은 code, `Default.table`, replacement map length, mode table,
Mesa/P3 allocator에 아직 사용하지 않는다.

```text
VRAM total = 32 MiB
VRAM type  = DDR
board P/N  = G45FMDVP32DSF
```

G2 통과에는 `R2_PHYSICAL_INSPECTION_RUN_SHEET.md`의 B2/B4 중 하나 이상으로
target board sticker 또는 memory-device marking을 확인하고, 이 candidate와
일치하는지 대조해야 한다. 불일치하면 candidate를 폐기하고 physical evidence를
우선한다.
