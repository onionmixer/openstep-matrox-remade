# D2-2c — 1 차 vertex 모드 대조군 (2026-08-28, 코딩 전)

D2-2b 의 계획은 죽었다: 정점은 `WDBR` 로 도착하지 `WR` 로 도착하지 않는다.
그 검토가 남긴 유일한 길이 이것이다:

> *"Primary VERTEX mode is costly as a production design, but the cost is
> irrelevant for a one-shot control."*

**W2 §24 가 1 차 vertex 모드를 기각한 이유는 생산 비용(run 3 개, +3.96 ms)이고,
삼각형 하나짜리 시험에서 그 비용은 0 이다.**

---

## 1. 무엇을 묻는가

**WARP 마이크로코드가 정점을 받아 삼각형을 그리는가.** 그것뿐이다.

수송 방식을 고르는 것이 아니다 — **문서화된 유일한 정점 경로로 한 번
그려 보는 것**이고, 그것이 되면 나중에 어떤 수송을 쓰든 *"마이크로코드와
상태는 옳다"* 를 알고 시작한다.

## 2. 왜 2 차 DMA 가 아닌가

| | 2 차 DMA | **1 차 vertex 모드** |
| --- | --- | --- |
| 새 수송 채널 | **있음** — 네 번 NO-GO | **없음** |
| 회수 필요 | 있음 (그리고 없음) | 1 차와 같음 |
| `SECADDRESS`/`SECEND` | 새 레지스터 | **안 건드림** |
| `PRIMPTR` 상태 쓰기 | `SECEND` 가 유발 | **해당 없음** |
| 생산 비용 | 낮음 | 높음 — **그러나 무관** |

**두 번째 열이 이 문서의 전부다: 새 채널을 열지 않는다.**

## 3. 두 run — 세 개가 아니다

W2 §24 는 **생산**을 위해 run 3 개를 셌다(`SOFTRAP` 기반 완료가 필요하므로).
**시험에는 둘로 족하다.**

```
run 1  GENERAL   컨텍스트 + 클립 + 파이프 기동 + SOFTRAP
                 -> 완료 판정: 기존 검증된 술어 (86,554 회)
run 2  VERTEX    PRIMADDRESS = phys | 0x3,  정점 24 dword
                 -> 완료 판정: ENDPRDMASTS + dwgengsts + wbusy/wbusy1
       그리고 읽는다
```

`PRIMADDRESS` 를 쓰면 pseudo-DMA 시퀀스가 리셋되고(4-15), `PRIMEND` 를 쓰면
`ENDPRDMASTS` 가 0 으로 떨어진다(4-17) — **run 2 의 판정이 run 1 의
`SOFTRAP` 에 오염되지 않는다.**

### 3.1 완료 술어에 `wbusy` 를 넣는다

D2-2b 검토의 지적: 기존 마스크는 `SOFTRAPEN`·`dwgengsts`·`ENDPRDMASTS` 뿐이라
**WARP 가 RUN/WAIT/STALL 이어도 완료로 읽힌다**(`STATUS.wbusy<18>`,
`wbusy1<19>`, 3-189).

```
완료 = ENDPRDMASTS && !dwgengsts && !wbusy && !wbusy1
```

## 4. 상태 — "DWGCTL, 목적지, 클립" 은 부족했다

DRM 이 컨텍스트로 내보내는 것 전부(`mga_state.c:96~`):

```
DSTORG MACCESS PLNWT DWGCTL ALPHACTRL FOGCOL WFLAG ZORG WFLAG1
TDUALSTAGE0 TDUALSTAGE1 FCOL STENCIL STENCILCTL
TEXCTL TEXCTL2 TEXFILTER TEXORG TEXBORDERCOL
```

그리고 **G400 의 클립 리셋 쌍**(`mga_state.c:48`) — `CLIPDIS` 를 끄는 유일한
문서화된 방법:

```c
DMA_BLOCK(DWGCTL, ctx->dwgctl, LEN+EXEC, 0x80000000,
          DWGCTL, ctx->dwgctl, LEN+EXEC, 0x80000000);
/* 그 다음에 CXBNDRY, YTOP, YBOT */
```

**이 쌍을 빠뜨리면 클립이 꺼진 채로 그린다** — 봉쇄가 무너진다.

## 5. 봉쇄 — "오프스크린" 은 주장이 아니다

| 강제할 것 | 값 |
| --- | --- |
| `DSTORG.dstmap` | 0 (프레임버퍼) |
| `ZORG.zorgmap` | 0, 또는 깊이 끔 |
| `TEXORG.texorgmap` | 0, 또는 **텍스처 끔** |
| `DWGCTL.clipdis` | 0 (§4 의 쌍으로) |
| `CXBNDRY`/`YTOP`/`YBOT` | 오프스크린 사각형에 딱 맞게 |
| `PLNWT` | 전부 1 |

**텍스처는 쓰지 않는다** — 단계를 줄이고 `texorgmap` 질문을 없앤다.

**카나리**: 삼각형 바깥 네 모서리와 클립 사각형 **밖** 네 지점을 sentinel 로
채우고, **그것들이 변하면 실패**로 판정한다. "세 꼭짓점 안이 변했다" 만
보면 밖에서 무슨 일이 났는지 모른다.

## 6. 마이크로코드 소유권

D2-2a 는 주소를 지역 변수에 두고 **일부러 흘렸다**(`:8072`) — 카드가 주소를
받았으므로 해제하지 못한다. **D2-2c 는 그 주소를 정적 변수에 보관한다.**
그러지 않으면 *"D2-2a 처럼 기동한다"* 가 재현 불가능하다.

## 7. 실패 정책 — 래치가 아니라 정지

D2-2b 검토가 옳았다: `runWarpPipeOnce` 는 `stormBlitFailed` 를 **세우지
않고**, 그 래치는 커널이 더 주는 것을 막을 뿐 **이미 도는 WARP 를 멈추지
못한다**. `OSMGAAccelRearm` 은 회수가 아니다. `RST.softreset` 은 쓸 수 없다.

> **도어벨 이후 타임아웃이면**: 버퍼를 전부 보유하고, 더 이상 엔진·모드
> 프로그래밍을 하지 않고, 재무장하지 않고, **재부팅을 요청한다.**

## 8. 목록 회계

```
run 2 = 정점 3 개 x 8 dword = 24 dword     (vertex 모드, 인덱스 없음)
run 1 = 컨텍스트 + 클립쌍 + 파이프 + SOFTRAP + 패딩
```

**24 dword 다** — 일반 모드였다면 30 이었다(D2-2b 가 그것을 틀렸다).

## 9. 판정

| | |
| --- | --- |
| 통과 | 클립 사각형 안에 **삼각형 모양**, 카나리 전부 무사 |
| 실패-상태 | 아무것도 안 그려짐 → 컨텍스트/파이프/마이크로코드 |
| 실패-봉쇄 | **카나리가 변함** → 즉시 중단, 재부팅 |
| 실패-엔진 | 완료 술어가 타임아웃 → §7 |

**이 시험은 수송을 묻지 않는다** — 문서화된 경로를 쓴다. 그러므로 실패는
**상태·마이크로코드·기하** 중 하나이고, 그것이 D2-2b 가 못 가진 판별력이다.

## 10. codex 에 물을 것

- run 2 의 완료 술어가 충분한가 (`ENDPRDMASTS && !dwgengsts && !wbusy`)
- vertex 모드 run 에 `PRIMEND` 의 `pagpxfer` 를 어떻게 두는가
- 정점 8 dword 의 **순서**가 맞는가 (x, y, z, rhw, diffuse, specular, tu0, tv0)
- 컨텍스트 목록에서 우리가 뺀 것(스텐실 등)이 문제가 되는가
- 파이프 선택 — `mga_warp.c` 의 어느 파이프여야 하는가
- 카나리 배치가 봉쇄를 실제로 증명하는가
