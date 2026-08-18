# R6 — G450 32bpp linear 완전 모드셋 (X.Org 정본)

기준일: 2026-08-18
출처: xenocara `xf86-video-mga` (`mga_dacG.c`, `mga_g450pll.c`, `mga_driver.c`,
`mga_reg.h`, `mga.h`) + `xserver .../vgahw/vgaHW.c`. 로컬 사본은 scratch/.

## 왜 이 문서

코드 리뷰(codex+독립 fork) + X.Org 대조로, 프로젝트의 byte-image 모드 모델이
작동하는 G450 모드에 **불충분**함이 드러났다. 최소 드라이버(CRTC 이미지 + PLL
M/N/P 단일 write)는 **검은 화면**이다. 원인: (1) CRTC 0x17 리셋해제 누락,
(2) DAC 픽셀포맷/DAC_EN 누락, (3) 시퀀서/그래픽스/어트리뷰트+PAS 비디오-ON 누락,
(4) PAN_CTL 누락, (5) G450 PLL은 raw write가 아니라 jitter-lock search 필요.

## MMIO 오프셋 (전부 mmioBase 기준)

```
Misc write 0x1FC2 / read 0x1FCC ; SEQ idx/data 0x1FC4/0x1FC5
CRTC 0x1FD4/0x1FD5 ; CRTCEXT 0x1FDE/0x1FDF ; GR 0x1FCE/0x1FCF
ATTR idx+write 0x1FC0 / read 0x1FC1 ; Input-Status-1 0x1FDA (FF reset)
DAC idx 0x3C00 / data 0x3C0A ; outMGAdac(i,v)/inMGAdac(i)
```

## 완전 write 시퀀스 (primary Gx50, 32bpp)

1. **ext clock 선택 + PLL lock**: `misc=read8(0x1FCC); write8(0x1FC2, misc|0x0C)`;
   그다음 **G450 PLL jitter-lock search**(아래) — 0x4C/0x4D/0x4E.
2. **PAN_CTL**: `outMGAdac(0xA2, 0x30)` (162MHz: 160000≤162000<175000 → 0x30).
3. **DAC 레지스터**(skip-filter 이후 실제 쓰는 것; 32bpp override):
   - `0x18 VREF_CTL=0x00`, `0x19 MUL_CTL=0x07`(32bpp), `0x1A PIX_CLK_CTL=0xC9`,
     `0x1D GEN_CTL=0x20`, `0x1E MISC_CTL=0x1F`(DAC_EN 필수).
   - 나머지 initDAC 기본값(0으로 두어도 무방한 것 다수). 최소 위 5개 + 0x00화.
4. **CRTCEXT[0..5]**: 1600×1200×32 → `[0]=0x10 [1]=0x41 [2]=0xAD [3]=0x83 [4]=0x00 [5]=0x00`.
   각 `write8(0x1FDE,i); write8(0x1FDF,ExtVga[i])`.
5. **Misc**: `write8(0x1FC2, 0x2D)` (vgaHWInit 0x23 | ext-clk 0x0C, VGA aperture off). +/+ 극성.
6. **SEQ**: SR00=0x00,SR01=0x01(screen-on),SR02=0x0F,SR03=0x00,SR04=0x0E. (SR1..4 write)
7. **CRTC[0..24]**: unlock(write CRTC 0x11 = val&~0x80) 후 0..24. 표준값 + **0x17=0xC3**, 0x14=0x00.
   1600×1200@60: `[0]=09 [1]=C7 [2]=C7 [3]=8D [4]=CF [5]=07 [6]=E0 [7]=00 [8]=00 [9]=00`
   `[10..15]=00 [16]=B0 [17]=23 [18]=AF [19]=90 [20]=00 [21]=AF [22]=E1 [23]=C3 [24]=AF`.
8. **GR[0..8]**: 00,00,00,00,00,**0x40**(GR05),**0x05**(GR06),0x0F,0xFF.
9. **AR[0..0x14] + PAS ON**:
   - flip-flop 리셋: `read8(0x1FDA)`.
   - `enablePalette`: `read8(0x1FDA); write8(0x1FC0, 0x00)`.
   - AR0..0x14 write(각 `read8(0x1FDA); write8(0x1FC0, index); write8(0x1FC0, value)`):
     AR00..0F=0x00..0x0F(identity), AR10=**0x41**, AR11=00, AR12=0x0F, AR13=00, AR14=00.
   - **PAS ON(비디오 켜기)**: `read8(0x1FDA); write8(0x1FC0, 0x20)`.
10. **CRTCEXT[0] 재기록**(start addr latch): `write8(0x1FDE,0); write8(0x1FDF,ExtVga[0])`.

## G450 pixel PLL jitter-lock search (`MGAG450SetPLLFreq`)

- MNP 후보표 생성(주파수→MNP; VCO=`(27000*(2*(N+2))+((M+1)>>1))/(M+1)`; P bit6=no-div,
  `f/=2<<(P&3)`; P bit3-5=S 밴드필터). 오차순 정렬.
- 각 후보에 대해 `writeMNP(mnp±0x300/±0x200/±0x100/0)` 순서로 쓰고 매번 lock 확인,
  모두 lock이면 채택. `writeMNP`= outMGAdac(0x4C,M);(0x4D,N);(0x4E,P).
- lock: `write8(0x3C00,0x4F)`; `read8(0x3C0A)` bit0x40, 초기 ≤1000 spin, 이후 100샘플 중 ≥90.
- 미채택 시 첫 후보라도 write.
- 우리 보수 시작안: 후보표 대신 검증된 M/N/P를 ±0x300..0 dither로 lock-search(핵심은
  jitter+통계 lock). 미lock→rollback→VGA(안전).

## 극성/Misc

+/+ (1600×1200@60): MiscOut bit6=0,bit7=0. 값 0x2D = IOAS(1)+clk(0x0C)+... , VGA aperture
비트(0x02) clear. OR가 아니라 **완전 바이트 write**로 극성 제어.

## 참고

이 값들은 X.Org verbatim. 프로젝트 생성기 CRTC 이미지는 일부 상이(vd off-by-one 등)
하므로, 실제 하드웨어 write는 **이 문서의 X.Org 값**을 정본으로 쓴다. transaction은
검증/시퀀싱/rollback 용도로만 유지(그 CRTC/PLL byte-image 출력은 하드웨어 정본이 아님).
