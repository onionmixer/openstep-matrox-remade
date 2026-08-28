# D2-2d — run 2 완료 규약 수정 (2026-08-28, 코딩 전)

D2-2c 는 **성공했고**(§13: 1176 px, 정확한 삼각형, 클립 밖 0) 판정 논리가 그것을
실패로 보고했다. 고칠 것은 하드웨어도 상태도 수송도 아니라 **완료 술어 하나**다.

## 1. 무엇이 틀렸나

```
run2 STATUS 0x80fe0024 :  endprdmasts=1  dwgengsts=0  wbusy=1  wbusy1=1
run1 STATUS 0x808e0021 :  endprdmasts=1  dwgengsts=0  wbusy=1  wbusy1=1
                                                      ^^^^^^^ run 1 부터 이미 1
```

3-189 는 `wbusy` 를 *"not idle; it may be RUNning, **WAITing**, STALLed or
loading microcode"* 로 정의한다. run 1 의 **목적이** WARP 를 기동해 대기 상태로
두는 것이므로, `wbusy=1` 은 성공의 징표이지 미완의 징표가 아니다.
`!wbusy` 는 **suspend 전에는 도달 불가능**하고 suspend 는 판정 뒤에 한다.

## 2. 고침 — 3 단계

```
단계 1  그리기 완료
        PRIMADDRESS(&~3) == vtxEnd  &&  ENDPRDMASTS  &&  !dwgengsts
        -> 연속 N 회 안정할 것

단계 2  WIADDR2 = WMODE_SUSPEND          (단계 1 통과 후에만)

단계 3  WARP 정지 확인
        !wbusy && !wbusy1
```

**`dwgengsts` 가 완료 비트다.** 3-189 가 그 1 조건으로 *bfifo 비었나 ·
**warpfifo** 비었나 · 그리기 엔진이 처리·전송 중인가 · **마지막 메모리 접근이
끝났나*** 를 모두 든다 — 정점 도착부터 픽셀이 메모리에 앉을 때까지 전 구간.

**`wbusy` 는 단계 3 으로 옮긴다.** 거기서는 의미가 분명하다 — *"그림이 끝났나"*
가 아니라 **"멈추라고 했을 때 멈췄나"**. 안 꺼지면 WARP 가 STALL 이었다는 뜻이고,
그것은 알 가치가 있는 진짜 실패다.

## 3. 왜 N 회 안정이 필요한가

`PRIMEND` 를 쓴 직후 정점이 아직 warpfifo 에 안 들어간 순간에는 `dwgengsts` 가
0 으로 읽힐 수 있다. 포인터 조건이 그 대부분을 막지만(24 dword 를 다 읽어야
끝 주소가 된다) 원자적 스냅샷은 아니다.

**"1 로 올라갔다가 0 으로 내려가는 것을 본다" 는 못 쓴다** — 1176 픽셀은 첫
폴 전에 끝날 수 있다. 첫 검토도 같은 지적을 했다. 그래서 **연속 안정**이다.

## 4. 채택하지 않는 대안

**run 3(GENERAL + SOFTRAP)으로 펜스를 친다.** 1 차 채널은 직렬이므로 vertex
목록 뒤에 GENERAL 목록을 넣으면 순서가 보장되고, `SOFTRAP` 완료 판정은 run 1 이
이미 증명했다. 그러나 **읽힘을 보장할 뿐 그림이 끝났음을 보장하지 않고**,
시험에 새 실패 모드를 하나 더 만든다. 단계 1 의 `dwgengsts` 가 이미 더 강하다.

## 5. 범위 밖

**삼각형을 두 개 그려 파이프에 반복 급전이 되는지** 는 다음 문제다(배치 정책).
지금은 **판정이 성공을 성공이라 말하게** 하는 것까지다.

## 6. 실패 정책 — 그대로

단계 1 또는 단계 3 타임아웃이면 §8 그대로: 아무 레지스터도 더 쓰지 않고,
링·마이크로코드 보유, `stormBusy` 유지, 판정 로그를 스캔보다 먼저, 재부팅 요청.
**단계 3 의 타임아웃은 STALL 된 WARP 이므로 특히 그렇다.**

## 7. codex 에 물을 것

1. `dwgengsts==0` 을 완료로 쓰는 것이 옳은가. 3-189 말고 반증할 근거가 있나.
2. 연속 N 회 안정이 적절한 방어인가. N 은 얼마여야 하나. 더 나은 신호가 있나.
3. 단계 2 의 suspend 가 **그림을 자를** 위험은 없나 — `dwgengsts=0` 이
   *"memory has not completed the last memory access"* 를 포함하므로 없다고 보는데.
4. 단계 3 이 안 꺼지는 경우, 그것이 STALL 이라는 해석이 맞나.
5. run 1 술어(`포인터 + SOFTRAPEN + ENDPRDMASTS`, `spins=18` 로 통과)는 그대로 둬도 되나.
6. 빠진 것.

---

## 8. codex 판정 (2026-08-28) — **내 §3 이 틀렸다**

| codex 주장 | 검증 | 결과 |
| --- | --- | --- |
| **연속 N 회 안정은 형식적 방어가 아니다. 정당한 N 이 없다** | 맞다. 전파 시간 상한을 주는 사양서 문장이 없으므로 N 을 늘리는 것은 **확률만 바꾸고 정확성은 못 바꾼다** | ✅채택 — **§3 을 폐기** |
| `DWGSYNC` 가 문서화된 완료 **이벤트**다 | 3-139: *"dwgsyncaddr 는 **DWGSYNC 가 프로그램되기 전에 보낸 프리미티브를 그리기 엔진이 완료했을 때에만** 프로그램한 값으로 갱신된다"* | ✅채택 |
| Windows 드라이버 패턴: 태그 +4, 빈 슬롯 2 대기, `DMAPAD`, `DWGSYNC`; 읽을 때 `0xfffffffc` 마스크 | 디스어셈블에 **그대로** 있다 — `add $0x4`, `cmp $0x2,%bl / jb`, `movl $0x0,0x1c54`, `mov %ecx,0x2c4c`; 폴링은 `and $0xfffffffc` (`:102524`, `:69701`) | ✅채택 |
| `DWGSYNC` 리셋값이 unknown 이므로 현재값에서 태그를 만들어라 | 3-139 이 *"Reset Value unknown"* 이라 적는다. `<1:0>` 도 Reserved | ✅채택 — wrap 포함 전 구간을 python 으로 확인 |
| `dwgengsts==0` 은 옳지만 **레벨**이라 제출 전에도 0 일 수 있다 | 사실이다. DRM 의 idle 판정도 `ENDPRDMASTS && !DWGENGSTS` 이고 wbusy 를 **뺀다**(`mga_drv.h:386`) | ✅채택 — 펜스 뒤 2 차 확인으로 유지 |
| 4 단계 타임아웃을 "STALL 이었다" 로 단정하지 마라 | 맞다. suspend 가 막힌 BFIFO 에 머무르는 경우·캐시 미스·상태 고착도 같은 관측을 낳는다 | ✅채택 — 문구를 *"WARP did not reach idle after the suspend request"* 로 |
| run 1 술어는 그대로 두라 | `spins=18` 로 통과했고 DRM 도 파이프 기동 후 `wbusy` 를 안 본다 | ✅채택(변경 없음) |
| GENERAL+SOFTRAP 3 번째 run 을 안 쓰는 판단은 옳다 | 3-175 이 SOFTRAP 은 **레지스터 접근**을 멈출 뿐이라 한다 | ✅ — 다만 *"§4 의 dwgengsts 가 이미 더 강하다"* 는 **내 문장이 틀렸다**: 제출 후임을 증명해야 비로소 강하다 |

### 8.1 최종 규약

```
1 xfer     (PRIMADDRESS & ~3) == vtxEnd && ENDPRDMASTS
2 fence    old = DWGSYNC & ~3 ; tag = (old+4) & ~3
           waitFifo(2) ; DMAPAD = 0 ; DWGSYNC = tag
           poll (DWGSYNC & ~3) == tag ;  그리고 !dwgengsts
           -> 여기서 삼각형이 메모리에 있다.  판정 스캔은 여기서.
3 suspend  waitFifo(1) ; WIADDR2 = SUSPEND
4 quiesce  poll !wbusy && !wbusy1 && !dwgengsts
```

**`wbusy` 는 4 단계에만 있다** — *"그림이 끝났나"* 가 아니라 *"멈추라니 멈췄나"*.

### 8.2 `PRIMPTR` 이 더 중요해졌다

3-166: `primptren1` 이 서면 **`DWGSYNC` 가 갱신될 때마다** 시스템 메모리에 쓴다.
이 규약은 `DWGSYNC` 를 반복해서 쓴다. 그리고 이 보드의 펌웨어가 남긴 값은
`0xfffffbf0` — **무장까지 한 비트 남은 4 GiB 근처 포인터**였다(`:3989`).
시작 전 `PRIMPTR == 0` 거부 조건은 이제 형식이 아니라 **핵심**이다.

---

## 9. 결과 (2026-08-28 20:20:59) — **PASS**

```
D2-2c/run1:    PRIMADDRESS 000501a4 (wanted 501a4), STATUS 808e0021, spins 18
D2-2c/run1:    changed 0 px -- 0 in triangle, 0 in clip only, 0 OUTSIDE CLIP
D2-2c/xfer:    PRIMADDRESS 00058063 (wanted 58060 | primod), STATUS 80be0020, spins 2
D2-2c/fence:   DWGSYNC 0 -> 4 (read 4), STATUS 80fe0024, spins 1
D2-2c/run2:    changed 1176 px -- 1176 in triangle, 0 in clip only, 0 OUTSIDE CLIP
D2-2c/run2:    bbox rows 8..55 cols 8..55, first value ff8040 (wanted ff8040)
D2-2c/suspend: STATUS 80f20024, spins 1
D2-2c: PASS -- WARP drew the triangle, fenced, and suspended
D2-2c: end (ring released, microcode kept)
```

### 9.1 STATUS 가 진단을 직접 증명한다

```
run1     softrapen=1 dwgengsts=0 endprdmasts=1 wbusy=1 wbusy1=1
xfer     softrapen=0 dwgengsts=0 endprdmasts=1 wbusy=1 wbusy1=1
fence    softrapen=0 dwgengsts=0 endprdmasts=1 wbusy=1 wbusy1=1
suspend  softrapen=0 dwgengsts=0 endprdmasts=1 wbusy=0 wbusy1=0   <- 여기서 처음 0
```

**지난 부팅이 타임아웃이라 부른 값 `0x80fe0024` 가 이번엔 fence 단계의 값이다.**
그리고 `WIADDR2 = SUSPEND` 를 **한 번 쓰자 spins=1 만에** `wbusy`/`wbusy1` 이
떨어졌다. WARP 는 STALL 이 아니라 **WAIT 였고, 멈추라니 멈췄다** —
옛 술어가 기다리던 조건은 suspend 없이는 영원히 오지 않았다는 직접 증거다.

### 9.2 펜스가 작동한다

`DWGSYNC 0 -> 4 (read 4)`, `spins=1`. 태그를 쓰고 그것이 되읽힌다.
`dwgengsts=0` 이 2 차 확인으로 함께 성립한다.

### 9.3 재현된다

`changed 1176 px, bbox rows 8..55 cols 8..55, ff8040` — **지난 부팅과 픽셀 단위로
동일**하다. 판정만 바뀌었고 하드웨어 거동은 같다.

### 9.4 머신은 깨끗하다

`end (ring released, microcode kept)` 까지 갔다 — 블록을 0 으로 복원하고,
`stormBusy` 를 반납하고, 링을 해제했다. 디스플레이 정상
(`linear mode ACTIVE 1600x1200 RGB:888/32`), 이후 로그 정상. **재부팅 불필요.**

### 9.5 spins 가 말해주는 것

```
run1 18 · xfer 2 · fence 1 · suspend 1        (한계는 100000)
```
전 단계가 즉시 끝난다. 옛 술어의 `spins=100000` 은 하드웨어가 느려서가 아니라
**도달 불가능한 조건을 기다렸기 때문**이었다.
