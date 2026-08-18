# H1 — Hardware interrogation decision (operator)

기준일: 2026-08-18
결정 주체: operator (physical target 소유자)

이 문서는 `HANDOFF.md`/`docs/TEST_STATUS.md`/R1 run sheet의 "MatroxMGA를
production owner로 유지, 하드웨어 미접근, Installer 전용" 전제를 **인터로게이션
단계에 한해** 갱신하는 operator decision을 기록한다. R6(owner test, mode
programming) 이후 단계의 게이트는 그대로 유효하다.

## 전제 변경 (operator가 실제로 바꾼 것)

1. **display owner가 MatroxMGA가 아니다.** target은 generic SVGA(IOVGADisplay,
   VESA BIOS mode 0x6a, 800×600 BW:2, legacy `0xA0000-0xBFFFF` + legacy VGA I/O
   만 사용) 하나만 등록해 부팅되어 있다. MatroxMGA는 로드조차 되어 있지 않고
   `System.config`의 Active Drivers에도 없다. 따라서 MGA G450의 native BAR
   region(high framebuffer aperture, MMIO, ILOAD)과 그 안의 VRAM/레지스터는
   현재 어떤 드라이버도 소유하지 않는다 → corruption 없이 조사 가능.

2. **복구 채널 = 단일 telnet 세션 하나로 확정.** 별도 physical/serial console은
   없다. 대응의 최선은 "문제 발생 시 로그를 최대한 남기는 것"이며, kernel log를
   NFS로 실시간 회수한다(`tools/nx-logcatch.sh`). VGA는 우리 코드와 독립적으로
   동작하는 recovery fallback 역할도 겸한다(Matrox 코드가 깨져도 화면 유지).

3. **드라이버 등록 방식 = openstep-intel1000 방식 채택.** Installer.app의 atomic
   package workflow 대신, loadable kernel server를 `kl_util`(add/load/status/
   unload/delete)로 등록·조사한다. 이는 G1 sole-owner의 최종 production 전환에는
   여전히 Installer/Configure가 필요하다는 것을 부정하지 않는다. 조사(probe)
   단계에만 적용한다.

4. **드라이버 네임스페이스 = `OpenStepMGA*`** (기존 `MatroxMGA`와 분리하여 혼동
   방지). operator 지시사항.

## 하드웨어 인터로게이션 확장 (read-only-PCI → staged)

VGA 소유 상태에서만 수행한다. 모든 스테이지는 IOLog→NFS로 결과를 회수하며,
mode 재프로그래밍·가시영역 write·display ownership 획득을 하지 않는다.

| stage | 얻는 것 | 조작 | 가역성 / 위험 |
| --- | --- | --- | --- |
| S0 | PCI 지문(vid/did/rev/class, command/status, BAR base, capability) | PCI config **read** only | 완전 read-only |
| S1 | BAR aperture 크기(FB/MMIO/ILOAD) | 각 BAR config에 write-1s → mask read → **원본 복원**(복원 readback 검증) | config space만, 완전 가역. legacy VGA 창은 command reg 미변경으로 계속 살아있음 |
| S2 | 현재 MMIO 레지스터 값(CRTC/PLL/status baseline) | BAR1 MMIO **read-only** map 후 read | write 없음 |
| S3 | 실장 VRAM 실측 | BAR0 map 후 고역 offset save→pattern write→aliasing readback→**원복**. 저역(VGA 가시영역) 회피 | VRAM write지만 save/restore로 가역 |

> S3 보류(operator, 2026-08-18): 16 MiB 보수 배치엔 실장 VRAM 실측이 불필요하며,
> VRAM 용량은 드라이버가 로드된 뒤 드라이버 자체 경로로 실측해도 된다. 따라서 S0~S2로
> interrogation을 종료하고 드라이버 착수(R4 skeleton→R6)로 넘어간다. probe에 S3
> 코드는 추가하지 않는다.

각 스테이지 후 체크포인트로 결과를 보고하고 다음으로 진행한다.

> 스테이지 명명 주의: 이 H1 interrogation의 S0~S3는 `R6_STORM_2D_SOURCE_AUDIT.md`
> 의 admission S0~S4(우리 드라이버가 owner가 되는 2D bring-up 시퀀스)와 **별개**
> 다. H1은 VGA가 화면을 소유한 상태의 수동 조사이며 mode programming/ownership을
> 하지 않는다.

## S2 read-only MMIO 레지스터 셋 (X.Org 원본 검증)

R6 audit 문서의 오프셋 표기("around 0x1c00", "around 0x1e10")가 부정확하여
xf86-video-mga `mga_reg.h`(openbsd/xenocara 미러) 원본으로 재확인했다. 읽을
대상은 BAR1(`0xe8200000`, 16 KiB=`0x4000`) 내부의 **read-only** 레지스터만:

| offset | 이름 | 성격 | 용도 |
| --- | --- | --- | --- |
| `0x1e20` | VCOUNT | read-only 자유진행 수직 라인 카운터 | **liveness/매핑/uncached 검증** — delay 두고 반복 읽어 증가 확인 |
| `0x1e14` | STATUS | read-only 상태 | 인터럽트/펜딩 상태 스냅샷 |
| `0x1e10` | FIFOSTATUS | read-only FIFO 상태 | 엔진 FIFO 상태 |
| `0x1e54` | OPMODE | 현재 operation mode | 현 엔진 모드 |
| `0x2e08` | MEMCTL | 메모리 제어(읽기 정보용) | VRAM 구성 힌트 |
| `0x1c00` | DWGCTL | draw control(write reg) | 원시 덤프만; write 아님 |

**절대 접근 금지**: `0x1e40` Reset(write 시 엔진 리셋), `0x0100` EXEC(write 시
실행 트리거). S2는 이들에 write하지 않으며 read도 하지 않는다. DAC/PLL은 indexed
접근(write 필요)이라 S2 범위 밖이다.

매핑: bare loadable이므로 `IOMapPhysicalIntoIOTask(phys,len,&virt)`로 BAR1을
매핑하고 read 후 `IOUnmapPhysicalFromIOTask`로 즉시 해제한다(한 CALL 내 완결).
이 함수는 cache 파라미터가 없어 cache mode가 미확정이나, S2는 read-only라
비파괴적이고 자기진단적이다: 매핑 실패(반환값 error)면 IODirectDevice
`mapMemoryRange:cache:IO_CacheOff` 경로 필요로 판단해 중단하고, VCOUNT가
변하지 않으면 cached/stale로 판단해 중단한다. 어느 경우도 하드웨어를 손상하지
않는다.

## 여전히 유효한 안전 규칙 (변경 없음)

- display anomaly / timeout / owner mismatch / recovery 채널 상실 시: 중지,
  증거 보존, known-good(현재 VGA) 상태 유지.
- R6(우리 드라이버가 mode를 프로그래밍하고 화면을 소유) 이전에는 CRTC/PLL/DAC
  write, engine command, mode 변경을 하지 않는다. S1~S3는 owner test가 아니다.
- 최종 G1 sole-owner production 전환은 Installer/Configure로 수행한다.

## Live 지문 (S0, VGA 소유 상태에서 측정)

```text
function  04:00.0
vid=102b did=0525 (G450) rev=85 class=030000 irq=11 pin=1
command=0007 (I/O+Mem+BusMaster) status=0290
BAR0 = 0xf8000000  prefetchable  -> framebuffer / VRAM aperture
BAR1 = 0xe8200000  non-prefetch  -> MMIO control aperture
BAR2 = 0xe8800000  non-prefetch  -> ILOAD / secondary
capability offset=dc id=01, offset=f0 id=02 ; VPD 데이터 접근 없음
```

## S1 결과 (BAR aperture 크기, config write-1s→restore)

```text
BAR0 reg=10 base=f8000000 size=02000000 (32 MiB) prefetchable -> framebuffer aperture
BAR1 reg=14 base=e8200000 size=00004000 (16 KiB)             -> MMIO control aperture
BAR2 reg=18 base=e8800000 size=00800000 (8 MiB)              -> ILOAD / pseudo-DMA
세 BAR 모두 restore-ok, command register 0007 불변, MMIO/VRAM 미접근.
```

FB aperture 32 MiB는 16 MiB 보수 배치의 주소공간을 여유롭게 포함한다. 단
이것은 aperture(주소창) 크기이며 실장 VRAM은 S3에서 별도 실측한다. 표준 MGA
G400/G450 레이아웃(32 MiB FB / 16 KiB MMIO / 8 MiB ILOAD)과 일치한다.

## S2 결과 (MMIO read-only, VGA 소유 하)

```text
IOMapPhysicalIntoIOTask(phys=e8200000, len=4000) -> virt=21446000  (bare loadable 성공)
dwgctl=00000000 fifostatus=00000210 status=80820000 opmode=00000000 memctl=00000000
vcount 8샘플(200us 간격): 0236 023e 0245 024c 0253 025a 0262 026a 0271  (+7~8 단조증가)
vcount-LIVE -> 매핑 uncached + aperture 디코드 + CRTC live scanout 증명
command-unchanged=0007, write 전무, Reset/EXEC 미접근, 클린 unmap, VGA 화면 무영향
```

증가율(+7.5 라인/200us)은 현재 800x600@60 VGA 모드의 스캔라인 주기(~26.5us)와
일치한다. 이 매핑 경로가 동작함을 확인했으므로, 실제 드라이버(R6)는 동일 물리
매핑을 IODirectDevice `mapMemoryRange:cache:IO_CacheOff`로 수행한다. memctl이 0으로
읽힌 것은 VGA/VESA BIOS 소유 상태의 값일 수 있으며 S2 목적(디코드/liveness 검증)에
영향 없다. 실장 VRAM 용량은 S3에서 확정한다.
