# R6 — G450 하드웨어 프로그래밍 시퀀스 (X.Org 검증)

기준일: 2026-08-18
출처: `xf86-video-mga` (openbsd/xenocara) `mga_g450pll.c`, `mga_dacG.c`,
`mga_driver.c`, `mga.h`. step2(모드 프로그래밍) 구현의 정본 시퀀스.

모든 접근은 BAR1 MMIO(`mmioBase`, cache-off)에 대한 byte(8) 또는 32비트 접근.

## DAC indirect 접근 (RAMDAC_OFFSET=0x3c00)

```
outMGAdac(reg,val): write8(mmio+0x3c00, reg); write8(mmio+0x3c0a, val)
inMGAdac(reg):      write8(mmio+0x3c00, reg); return read8(mmio+0x3c0a)
```

## 레지스터/오프셋

- pixel PLL: `PIX_PLLC_M=0x4c, N=0x4d, P=0x4e`, `PIX_PLL_STAT=0x4f`(lock bit `0x40`)
- `PAN_CTL`(loop filter, Gx50), `PIX_CLK_CTL=0x1a`
- MiscOutput write `mmio+0x1fc2`, read `mmio+0x1fcc`; `CLKSEL_MGA=0x0c`
- SEQ idx/data `0x1fc4/0x1fc5`
- CRTC idx/data `0x1fd4/0x1fd5`; CRTCEXT idx/data `0x1fde/0x1fdf`
- 입력상태1(retrace) `mmio+0x1fda` bit `0x08`; VCOUNT `mmio+0x1e20`
- MNP 패킹: `(M<<16)|(N<<8)|P`

## G450 pixel PLL set + lock (`MGAG450SetPLLFreq`)

순서:
1. **clock source 선택(MNP write 전, 1회)**: `ucMisc=read8(0x1fcc); write8(0x1fc2, ucMisc|0x0c)`.
2. 후보 MNP 계산(주파수→MNP; VCO=`(27000*(2*(N+2))+((M+1)>>1))/(M+1)`; P bit6=no-divide, `f=f/(2<<(P&3))`; P bit3-5=S 밴드필터: VCO<550000→0 … <1300000→4 else 5).
3. **jitter-search**: 각 후보에 대해 `writeMNP(mnp±0x300)`,`±0x200`,`±0x100`,`0`를 순서대로 시도하고 매번 lock 확인. 모두 lock이면 그 후보 채택.
   - `writeMNP`: `outMGAdac(0x4c,M); outMGAdac(0x4d,N); outMGAdac(0x4e,P)`.
4. **PAN_CTL** 밴드값 write(PLL 직후, 안정화 필수): `outMGAdac(PAN_CTL, panBand)`.

lock 판정 `G450IsPllLocked`:
```
write8(0x3c00, 0x4f)             # index PIX_PLL_STAT
spin: read8(0x3c0a) until bit0x40 set, up to 1000회
if within 1000: 100회 샘플 read8(0x3c0a), bit0x40 set 카운트
locked = (count >= 90)
```

> step2 시작안(보수적): 계산된 M/N/P 1회 write → 위 통계적 lock 판정을
> bounded-poll로 감쌈 → 미lock 시 rollback→VGA. 필요시 jitter-search(3) 이식.

## CRTC / 모드 restore 순서 (`MGAGRestore`, primary head)

1. (Gx50) 위 PLL set + `PAN_CTL`.
2. DAC 인덱스 레지스터 restore 루프(0x00..0x4f, skip filter; Gx50는 0x4c-0x4e·0x2c-0x2e skip — PLL을 위에서 이미 set).
3. **CRTCEXT[0..5] restore**: 각 `write16(0x1fde, (ExtVga[i]<<8)|i)` 또는 byte로 `write8(0x1fde,i); write8(0x1fdf,ExtVga[i])`.
4. 표준 VGA restore: MiscOutput(`|=0x0c`), SEQ reset, 표준 CRTC[0..24], attribute/graphics.
5. **CRTCEXT0 재기록**(start address 확정): `write16(0x1fde,(ExtVga[0]<<8)|0)`.

표준 CRTC 값(`MGAGInit`): CRTC[3]=`(ht&0x1F)|0x80`, CRTC[17]=`(ve&0x0F)|0x20`(bit7 write-protect=0), MiscOut `|=0x0c`. (프로젝트 CRTC byte image가 이 값을 담고 있어야 함 — readback 검증으로 확인.)

## Linear framebuffer enable (CRTCEXT3 bit 0x80)

`ExtVga[3] = ((1<<BppShift)-1) | 0x80` (24bpp는 `((1<<BppShift)*3-1)|0x80`). bit7(0x80)=MGA-mode/linear-FB enable. SyncOnGreen 시 `|=0x40`.

런타임 토글:
```
write8(0x1fde,3); tmp=read8(0x1fdf); write8(0x1fdf, tmp|0x80)
```

## Display start address (`MGAAdjustFrame`)

20비트 base = CRTC[0x0D](b0-7) | CRTC[0x0C](b8-15) | CRTCEXT0 low nibble(b16-19).
vsync 대기 후 기록:
```
while(read8(0x1fda)&0x08);      # retrace 끝 대기
while(!(read8(0x1fda)&0x08));   # retrace 시작 대기
# VCOUNT(0x1e20)로 past-start 확인
write16(0x1fd4, (base&0xFF00)|0x0C)         # 또는 byte 2회
write16(0x1fd4, ((base&0xFF)<<8)|0x0D)
write8(0x1fde,0x00); tmp=read8(0x1fdf); write8(0x1fdf,(tmp&0xF0)|((base&0x0F0000)>>16))
```

## G450 gotchas (verbatim comment 근거)

- G450 PLL은 **1회가 아니라 여러 번 write + 통계적 lock**. 단일 write는 첫 시도에 미lock 가능.
- clock source(MiscOutput 0x0c)를 MNP write **전에**.
- PAN_CTL(loop filter)를 PLL 직후 write해야 안정.
- Gx50는 일반 DAC PLL restore를 우회(0x4c-0x4e skip), `MGAG450SetPLLFreq`로 별도 set.
