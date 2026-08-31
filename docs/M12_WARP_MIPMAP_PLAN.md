# M12 — WARP 경로 밉맵 (계획, 2026-08-31, 코딩 전)

목적: 원거리 텍스처 시머링 제거 — GLQuake 품질 격차의 마지막 항목.
전제가 되는 역사: **M1-4D9 가 사다리꼴(TEXTURE_TRAP) 경로에서 밉 fetch 가
일어나지 않음을 실측으로 닫았다** — MIN 의 mm*s 인코딩은 기준 레벨의 별칭.
"밉맵은 WARP 쪽" 은 4D9 §11 이 남긴 **미검증 추측**이다.  그러므로 이 계획은
구현이 아니라 **자격검증 프로브가 1단계**다.

## 1. 확인된 사실

### 1-1 하드웨어 모델 (G400SPEC)
- TEXFILTER.minfilter 4비트: NRST/BILIN + **MM1S/MM2S/MM4S/MM8S** =
  GL 의 네 밉 필터와 1:1 (9603-9617).  MM16S=ANISO 는 범위 밖.
- TEXFILTER.mapnb <31:29,18> = 사용 맵 수 0~10 (9651).
  fthres 4.4 고정소수 = min/mag 전환 문턱 (9645-9650).
- 레벨 원점: **offsetselect=0000 이면 TEXORG·TEXORG1~4 가 LOD 0~4 의 독립
  절대 원점** (Table 4-2).  5 LOD 초과는 offsetsel 체이닝(Table 4-3~) — 원점
  하나에서 >>2 시프트 합으로 유도(연속 배치 강제).
- PITCHLIN + 밉맵이면 **tpitchext 는 (16의 배수 − 1)** 제약 (9273-9276) —
  현 인코더는 pitch==width 를 그대로 싣는다: 밉 모드에서는 pitch 인코딩
  재검토 필요(비트 해석: 값-1 표기인지 실측 확인 대상).
- TEXWIDTH/TEXHEIGHT 에 twmask/rfw(rfh) — rfw 는 4D9 §9-4 가 "합법 폭
  전부에서 무반응" 까지만 확인, 의미 미해결.

### 1-2 이미 있는 플러밍
- `OSMGA_HW3D_TEXF_MINMODE_*` (비트 9-12): 검증기가 네 모드만 허용
  (HW3D.c:542-549), TEXFILTER 인코딩 반영(:1855), **v9·v10 공용 인코더가
  이미 MIN 필드로 흘린다**(.m:15520 v9; v10 은 tex->filter 경유).
- 4D9 의 아틀라스 프로브 기법: 비선형 주소 서명((r²+g²)&255), 대조군 2종,
  네 모드 × 네 배율 × 행 전체 비교 — 전부 검증된 방법론.
- 훅 게이트: MinFilter NEAREST/LINEAR 만 허용(Hook.c:3228) — 개방은 Phase B.
- 아레나: 정렬 32B, first-fit, 블록 정수 관리 — 다중 레벨은 "체인 전체를
  한 블록" 이 단순(레벨별 원점은 블록 내 오프셋으로 산출).

### 1-3 Mesa/quake 쪽
- Mesa 3.4.2 는 Image[level] 전 레벨 보관, 완결성 검사 후 t->Complete.
- GLQuake 는 GL_Upload8 이 1×1 까지 전 레벨 업로드; 포트는 현재 매 프레임
  GL_LINEAR 로 강제 복구(제거는 Phase B 마지막).
- 세계 텍스처 최대 128×128(=8 레벨) — **mapnb≤4(5 레벨: 128→8)로 캡**하면
  단순 모드(독립 원점 5개)로 충분하고 λ 클램프는 하드웨어 mapnb 가 한다.
  8×8 미만까지 내려갈 일은 λ>4 초원거리뿐 — 시머링 제거 목적에는 5 레벨로
  충분(문서화되는 의도적 절충).  GL 완결성은 Mesa 에 그대로 두고(소프트웨어
  폴백 경로 보존) 하드웨어만 캡.

## 2. Phase A — WARP 밉 fetch 자격검증 프로브 (재부팅 1)

4D9 §2 의 실험을 **v10/WARP 제출로** 재실행한다.  커널 시험 섹션(M4 방식,
Instance 테이블 게이트 + setIntValues 트리거)으로:

```
아틀라스: LOD0 64×64 (주소 서명), 바로 뒤 LOD1 32×32 (다른 서명),
          LOD2 16×16, 경계 밖 제3 서명 — 4D9 §73 레이아웃 재사용
설정:     TEXORG=LOD0, TEXORG1=LOD1, TEXORG2=LOD2 (절대 원점, offsetsel=0)
          mapnb=2, fthres 기본, minfilter=MM1S→MM2S→MM4S→MM8S
드로우:   WARP 배치(v10)로 축소 배율 2·4·8 삼각형, 대조군(NRST/BILIN) 선행
판독:     행 전체 되읽기 → 서명 복호 → 어느 레벨을 fetch 했는가
```

판정 사다리:
1. 대조군이 LOD0 주소만 → 디코더 정상 (전제)
2. mm*s 가 LOD1/LOD2 서명을 읽으면 → **WARP 경로에 밉 fetch 실재** → Phase B
3. 전부 LOD0 이면 mapnb·fthres 스윕(재부팅 없이 파라미터만) 후에도 같으면
   → **이 실리콘/경로에 밉 fetch 없음으로 종결** — 4D9 와 같은 강도로 닫고,
   시머링은 "구현 불가(하드웨어)" 로 기록. (음성도 가치 있는 결론)

안전: 읽기 전용 원칙 — 검증기가 밉 모드+mapnb>0 이면 **모든 선언 레벨의
원점·범위가 증명된 창 안**임을 요구(4D9 채택 규칙의 확장).  파이프는 기존
D2C_PIPE(tgzsaf) 그대로 — 밉은 레지스터 측 상태다(마이크로코드 가설이
틀렸다면 3에서 드러난다).

## 3. Phase B — 생산 설계 (프로브 양성일 때만; 재부팅 1)

1. **아레나**: 체인 한 블록 할당(Σ 레벨, 각 레벨 32B 정렬 오프셋), TexRes 에
   levelCount·levelOff[5].  업로드는 Image[0..n] 복사(기존 osmgaTexCopy 를
   레벨 루프로).  TexSubImage 무효화는 전 레벨 invalid (현행 그대로).
2. **계약(OSMGAHW3DState/Tex)**: texorgN[4]·mapnb 추가(버전 자리 확인),
   v10 인코더가 TEXORG1..4·mapnb·fthres 를 싣고 pitch 제약(16의 배수-1
   해석 확정치) 적용.  검증기: 레벨별 창 검사 + 네 모드 + mapnb≤4.
3. **훅**: MinFilter 밉 4종 admission 개방, MINMODE 플래그 설정,
   완결성(Complete)·레벨 치수 일치 검사 후 아니면 기존 폴백.
4. **포트**: GL_LINEAR 강제 복구 제거, 기본 `gl_texturemode
   GL_LINEAR_MIPMAP_NEAREST` 로.
5. 검증: 회귀(비밉 경로 불변, 130ms↔), 시각(원거리 시머링 소멸 — 사용자),
   mm8s(트라이리니어) 성능 측정(대역폭 2×샘플 — fps 영향 보고 후 기본값 결정).

## 4. codex 에 묻는 것

1. Phase A 설계가 4D9 의 음성 결론과 **가르는 변인**을 정확히 하나(제출
   경로)로 만들었는가?  WARP 라서 함께 달라지는 숨은 변인(NOPERSP 부재,
   TDS 설정, 파이프 microcode)이 fetch 에 영향을 줄 수 있는가 — 있다면
   프로브 행렬에 어떤 축을 더해야 하나.
2. offsetsel=0 단순 모드 + mapnb≤4 캡 절충의 함정 — GL 관점(λ 클램프 위치,
   완결성), 하드웨어 관점(mapnb 와 TEXORGn 미설정 레벨의 fetch).
3. "(16의 배수 − 1)" pitch 제약의 올바른 해석과 실측 방법.
4. TexRes/아레나 체인-한-블록 설계의 반례(레벨별 pitch=width 유지 시 32B
   정렬과 TEXORG <31:5> 256-bit 정렬 요건 충돌 — TEXORG 는 32B 정렬 필요!
   레벨 오프셋도 32B 배수여야 함 확인).
5. fthres 기본값(현 FTHRES1)이 min/mag 전환에 주는 영향 — 밉 모드에서
   적정값.
6. 빠진 것.

## 5. codex 교차검토 판정 (2026-08-31) — Phase A 조건부 GO

| codex 주장 | 내 검증 | 판정 |
|---|---|---|
| **scratch/mga-dri-xf410 에 5-LOD WARP 밉 구현 선례 전체가 있다** — MAXLEVELS=5, 레벨 32B 정렬·최소 8×8, TEXORG1..4 절대 원점, mapnb=LastLevel<<29, 비선형 tpitch=log2(w)-3 | mgatex.c/mgatexmem.c/mgacontext.h 직접 확인 | ✅채택 — Phase A 기준 설정을 DRI 선례로 교체, 음성 시 "하드웨어 미지원" 단정 금지 |
| 스펙 MM2S/MM4S 의 GL 매핑이 DRI 와 **뒤바뀜** | mgatex.c:107 vs 스펙 9612-9614 확인 | ✅채택 — 네 모드 전수 측정 사유 추가 |
| 아레나 정렬은 32B 가 아니라 **64B** | TexArena.c:38,104 확인 | ✅정정 |
| 현 WARP 빌더는 TEXORG1..4 를 **0 으로** 쓴다 — 기존 빌더 재사용 불가, 미사용 원점도 유효 원점 반복이 보수적 | .m:8918-8922 확인 | ✅채택 — M12 전용 descriptor/빌더 |
| 공개 v10 ABI 를 Phase A 에서 확장하지 말 것(미초기화 위험) | M4 스택 채움 방식 논거 타당 | ✅채택 |
| 검증기: mapnb≤4·레벨별 32B 정렬·정확한 반감 치수·레벨별 창 검사·오버플로 | 현 검사(행 2배)의 한계 확인 | ✅채택 |
| pitch "(16×n)−1" 해석 + Phase A 는 비선형 tpitch 로 회피, PITCHLIN 63/64 는 양성 후 진단 arm | 문법·LOD 시프트 정합 논거 | ✅채택 |
| 변인 서술 정직화: "제출 경로 하나" 가 아니라 "WARP front-end 전체"; affine-rhw/persp-rhw 두 arm, NOPERSP 토글은 진단용 | TEXWIDTH/HEIGHT WARP 부호화 상이(HW3D.c:1444) 확인 | ✅채택 |
| 프로브는 read-only 가 아니라 "검증된 오프스크린 밖에 쓰지 않는 비파괴" — WARP/DMA 상태·타임아웃 래치 표면 존재 | 타당 | ✅채택 — 표현 정정 |
| fthres 0x10 시작, 진단에 0x20; NRST/BILIN 대조가 WARP 축소에서 갈리는지 선행 확인 | DRI mgatex.c:103 확인 | ✅채택 |
| TEXCTL2 에 mip enable 없음; 4D9 §11 의 무명 레지스터는 SEC*/SOFTRAP/DR0 — 건드리지 말 것 | 스펙 1267 확인 | ✅채택 — 음성 사다리 정리 |
| 라이트맵은 비밉(min/mag LINEAR·level0 만) → 체인 비용이 핫패스에 안 붙음; 단 "전 레벨 invalid 는 현행" 이라는 내 서술은 오류(레코드는 이미지별) | gl_rsurf 1682-1685·Texture.c:41 확인 | ✅채택 — Phase B 에 객체 단위 재설계 명시 |
| 아레나: 블록 수는 거의 불변(체인=한 블록), 바이트만 ~4/3; 얇은 텍스처는 8×8 패딩으로 더 큼 | 논거 타당 | ✅채택 |

### Phase A 확정 스펙 (승인 조건 반영)
- 기준 설정 = DRI 선례: 비선형 tpitch(=log2(w)-3), WARP 치수 부호화, TEXORG0..2
  절대 원점 + TEXORG3/4 도 유효 원점으로 채움, mapnb=2(=3레벨, LastLevel 부호화
  host test 로 고정), fthres 0x10.
- M12 전용 커널 descriptor/리스트 빌더 + 레벨별 범위 검증기 (공개 ABI 불변).
- 행렬: {affine-rhw, persp-rhw} × {NRST, BILIN, MM1S, MM2S, MM4S, MM8S} ×
  축소 2·4·8.  LOD별 상수색+주소 서명 이중 부호(트라이리니어 혼합 판독).
- 음성 사다리: NRST/BILIN 분화 확인 → mapnb 1/2/4 → fthres 0x20 →
  avgstride → 합법 rfw/rfh → PITCHLIN 63/64.  그래도 음성이면 결론은
  "정본 WARP 상태에서 관측 실패" 로 제한 (DRI 선례 때문에 미지원 단정 금지).
- 타임아웃 = 기존 WARP 래치 정책 그대로 (재부팅 1회 예산).

## 6. Phase A 실측 결과 (2026-08-31) — **양성: WARP 경로에 밉 fetch 실재**

두 실행(v1 은 syslog 유실·분석기 결함으로 참고용, v2 가 정본).  v2 는 픽셀
단위 diff(대조군과 바이트 동일=별칭, 둘 다와 상이=fetch) + 레벨별 분리 G
대역 서명.  축소율 4(λ=2), mapnb=2(3레벨), 16px 다리.

```
        [affine 0-9]                          [perspective 10-15]
[5] mm8s  dN136 dB136  전량 혼합(bad136)   [10] nrst   dN0   L0 136  (대조군)
[6] m1n1  dN136 dB136  L1 136  bad0       [11] bilin  dN31  L0 105 bad31
[7] m1n4  dN136 dB136  L2 136  bad0       [12] mm1s   dN136 dB136  L2 136 bad0
[8] m1f2  dN136 dB136  L2 136  bad0       [13] mm2s   dN136 dB136  L2 136 bad0
[9] m8f2  dN136 dB136  전량 혼합          [14] mm4s   dN136 dB136  L2 105 bad31
                                          [15] mm8s   dN136 dB136  L2 105 bad31
(affine 0-4 행은 두 실행 모두 syslog 에 밀려 유실 — 6-8 이 affine 의
 MM1S 변형들을 깨끗한 L1/L2 로 보여 좌표는 닫힘)
```

### 판정
1. **밉 fetch 실재**: MM1S/MM2S 가 두 대조군과 전 픽셀 상이하면서 서명
   오류 0 으로 **정확히 레벨 2** 를 읽음 — 별칭 불가능한 결과.
2. **LOD 산술 정확**: 축소 4 → λ=2 → 레벨 2.  mapnb=1 이면 λ 가 1로
   클램프되어 레벨 1 (m1n1).  mapnb=4(원점 중복)도 L2 (m1n4).
3. **레벨 간 혼합**: MM4S/MM8S 는 perspective 에서 L2 105 + 혼합 31(경계),
   affine 정수 λ 에서 전량 혼합 — *_MIPMAP_LINEAR 계열 동작.  λ 정수점의
   혼합 가중은 Phase B 의 fthres/반올림 조사 항목.
4. **스펙 대 DRI 명명**: MM2S 가 단일 레벨(=\*_MIPMAP_NEAREST), MM4S 가
   혼합(=\*_MIPMAP_LINEAR) — **스펙 9612-9614 의 매핑이 옳고 DRI 쪽 표기가
   뒤집혀 있다.**
5. fthres 0x20 은 결과 불변 (m1f2).

### 다음
Phase B(생산 구현)는 계획 §3 대로 — 착수 전 별도 codex 검토.  추가로 Phase
B 조사 항목: λ 정수점 혼합 가중, affine/persp 혼합 차이, MM2S/MM4S GL 매핑
확정치 반영(스펙 기준).

부산물 교훈: 요약은 꼬리에서도 16줄이면 머리가 밀린다 — 다음 밴드는 10줄
이하로.  4D9 의 "trapezoid 밉 없음" 은 여전히 참이며, 두 경로의 차이가
이로써 실측으로 갈렸다.

## 7. Phase B 설계 확정 (2026-08-31, 코딩 전 — codex 검토 대상)

### 7-1. 계약 (hw3d 헤더, 양측 동시 재빌드)
- `OSMGAHW3DState` 끝에 `mipMapnb`(0=밉 없음, 1..4)와 `mipOrg[4]`(레벨 1..4
  절대 원점) 추가.  **WARP 배치 version 10 → 11** — 커널은 11만 수락,
  구판 라이브러리는 깨끗이 거절(레이아웃 lockstep 강제).  v9 계약 불변.
- 필터는 기존 `texFlags` 의 MINMODE 비트(9-12)를 그대로 탄다 — 검증기·
  인코더가 이미 알고 있는 채널.  GL 매핑(Phase A 확정, 스펙 기준):
  NEAREST_MM→MM1S(0x8), LINEAR_MM_NEAREST→MM2S(0x9),
  NEAREST_MM_LINEAR→MM4S(0xA), LINEAR_MM_LINEAR→MM8S(0xC).

### 7-2. 커널
- `OSMGAM3Tex` 에 `mipMapnb`+`mipOrg[4]` 추가; **기존 스택 채움 지점 9곳
  전부에 명시적 0 초기화**(미초기화 위험은 codex 가 Phase A 에서 지적).
  M12 밴드는 static 훅(osmgaM12MipOrgs)을 버리고 이 필드를 쓴다 — static
  제거.
- `osmgaWarpTexFromState`: state.mipMapnb>0 이면 tex 필드 채움 + filter 에
  mapnb<<29 합성.
- 빌더: `tex->mipMapnb>0` 이면 TEXORG1..4 = mipOrg(미사용 칸은 마지막 유효
  원점 반복), texctl 은 비선형 tpitch(log2(w)-3).  0 이면 현행 그대로.
- 검증기(`osmgaHW3DValidateStateCommon` 텍스처 절): MINMODE≠0 ↔ mapnb∈1..4
  상호 필수; TW32·pow2·pitch==texW·최저 레벨 dim≥8(mapnb ≤ log2(texW)-3);
  각 레벨 원점 32B 정렬 + 레벨 footprint 가 texStart..texEnd 안(halving
  치수로 계산); `osmgaHW3DWarpAdmits` 는 mapnb 동반 시 네 MINMODE 허용.

### 7-3. 유저랜드 텍스처 (Texture.c / TexArena)
- `OSMGAMesaTexRes` 에 `levelCount`, `levelOff[5]` — **체인은 한 블록**
  (Σ align32(레벨 바이트), 레벨 치수 128..8 은 패딩 불요), 레코드는 base
  image 의 DriverData 하나.
- 신규 `OSMGAMesaTexResidentMip(ctx, wantMip, ...)`: wantMip 이면 Mesa
  완결성(`t->Complete`)·pow2·레벨 존재를 확인하고 체인 업로드(레벨별
  osmgaTexCopy), mapnb=min(log2(dim)-3, 레벨수-1, 4) 반환.  비밉 텍스처는
  현행 단일 레벨 그대로.
- 무효화: TexImage/TexSubImage 훅이 **어느 레벨을 만져도 base 레코드**를
  drop/invalid (체인 전체 lazy 재복사).  라이트맵 등 비밉 객체는 영향 없음
  (단일 레벨 경로 유지 — 핫패스 비용 불변).
- 단일↔체인 전환(필터가 나중에 밉으로 바뀜): 레코드 종류 불일치 시 drop 후
  재업로드.

### 7-4. 훅 (Hook.c)
- admission(3228): 네 밉 MinFilter 허용 — 단 텍스처가 pow2·완결일 때만,
  아니면 기존대로 소프트웨어.
- 텍스처 준비(2638): 밉 필터면 Mip 변형 호출, pendTexMip{Mapnb,Org[4]}
  보관, texFlags 에 MINMODE 합성; fillState 두 곳(1310, 1823)이 state 로
  복사.  배치 키는 level-0 origin 그대로(체인 원점=키).
- 기본 경로 주의: quake 기본 필터 = LINEAR_MIPMAP_NEAREST(MM2S) — Phase A
  에서 가장 깨끗했던 모드.  MM8S 의 정수-λ 혼합 특성은 문서화만.

### 7-5. 포트 (gl_vidsdl.c)
- GL_LINEAR 강제 복구 제거, 필터 감사 허용 집합 갱신.  gl_texturemode
  기본( LINEAR_MIPMAP_NEAREST )이 하드웨어로 가게 된다.

### 7-6. 검증
- 호스트: 검증기 신규 케이스(hw3d 호스트 시험) — mapnb/MINMODE 상호성,
  레벨 창 이탈, 정렬, pow2, 최저 dim.
- 실기(재부팅 1): ① qual run — 기존 밴드 전부 + M12(struct 경유) 불변
  ② glquake: 원거리 시머링 소멸(육안), 120ms 회귀, declined 0, 텍스처
  업로드 수(체인 비용) 관찰.

### 7-7. codex 에 묻는 것
1. 계약 확장 위치·version 11 처리(스냅샷 공용체 크기, 디스패처, 구판 거절
   경로)의 함정.
2. 검증기 레벨 footprint 산식(halving + 32B 정렬)과 admission 미러링의 구멍.
3. base-레코드 무효화로 바꿀 때 기존 단일 레벨 경로(라이트맵 SubImage)의
   회귀 위험 — 레코드 소유가 base image 로 옮겨질 때 TexImage(level>0)
   redefinition 처리.
4. GL λ 클램프 차이(mapnb 캡 vs GL 전체 체인)를 "정확 가속" 으로 광고할 수
   있는가 — 아니면 admission 을 레벨 수 일치 때만 열어야 하는가.
5. 전환(단일↔체인) 및 아레나 단편화·바이트 여유의 반례.
6. 빠진 것.

## 8. Phase B codex 판정 (2026-08-31) — GO-with-changes, 필수 6조건 채택

| 조건 | 내용 |
|---|---|
| 1 | **v9 ABI 동결**: 밉 필드는 OSMGAHW3DState 가 아니라 **WARP v11 배치 전용 멤버**(state 뒤 osmga_u32 mipMapnb, mipOrg[4]).  VERSION_WARP 10→11, 디스패처는 9/11 만 명시 수락.  호스트에 sizeof(State)·v9 tri 오프셋·배치 크기 동결 검사 추가 |
| 2 | 검증기: 레벨별 footprint(Reach, halving, 32B 정렬) + **양축** min-dim 8 (mapnb ≤ min(log2W,log2H)-3) + MINMODE↔mapnb 상호 필수.  v9 진단용 double-rows 규칙은 유지(선택자 경로), 생산 규칙은 v11 메타데이터에만 |
| 3 | 정확성: admission 은 `mapnb == P - BaseLevel` 일 때만 — 포트가 밉 객체에 GL_TEXTURE_MAX_LEVEL 을 8×8 레벨로 캡(기존 필터 감사 순회 확장).  MinLod/MaxLod/LodBias 비기본 → 소프트웨어 |
| 4 | 밉 상태의 WARP 선행 거절 폴백은 **소프트웨어** (v9 사다리꼴 금지 — 밉 fetch 없음 실측 경로) |
| 5 | 1차 개방은 **MM1S/MM2S** 만 (quake 기본 = MM2S).  MM4S/MM8S 는 λ 정수점 혼합·fthres 경계를 Mesa 오라클과 대조한 뒤 |
| 6 | pending 텍스처 키에 mipMapnb·mipOrg 포함; 모든 배치/프로브 초기화 경로에서 밉 필드 명시 0; 레벨별 usable(Data≠0·비압축·border0) 매 조회 검사; 랩 게이트의 공간 필터 분류(MM1S=nearest계, MM2S=linear계) |

버전 거절 경로 검증 완료: 구 클라이언트 v10 → v9 낙하 → E_VERSION/EINVAL →
훅 재생 → 8회 후 revoke (영구 재생 없음).  체인 아레나: first-fit 연속 gap
실패 시 소프트웨어(성능만), 블록 수는 오히려 유리.

## 9. M12-C — MM4S/MM8S 혼합 가중 자격검증 (계획, 2026-08-31, 코딩 전)

목적: MM4S(=NEAREST_MIPMAP_LINEAR)/MM8S(=LINEAR_MIPMAP_LINEAR, 스펙 9612-9614)의
레벨 간 혼합 가중 f 를 Mesa 오라클과 실측 대조해 개방/기각을 판정한다.
Phase A 잔여 질문(§6-3): λ 정수점의 혼합 가중, 분수점의 가중 정확도.

### 9-1. 오라클 (Mesa 3.4.2 texture.c)

`COMPUTE_LINEAR_MIPMAP_LEVEL`(:378): λ 를 [0, M] 으로 클램프, level=floor(λ),
`f=frac(λ)` 로 level/level+1 을 (1-f)/f 선형 혼합 (:1006 nearest, :1032 linear).
- **λ 정수점: f=0 → 하위 레벨 순수.**  λ≥M: 최상위 레벨 순수.
- fthres 는 min/mag 전환 문턱일 뿐(스펙 9646-9650) 혼합 가중과 무관 —
  Phase A 의 m1f2/m8f2 불변이 이를 실측으로 뒷받침.  이번 밴드는 0x10 고정.

### 9-2. 방법 — M12 밴드 v3 (D2-2c 옵트인, 재부팅 1회)

v2 의 틀(아틀라스 15MB, 64/32/16 3레벨, affine 16px 다리)을 유지하고 세 가지만
바꾼다:

1. **채움 단순화**: 레벨 내 G 를 상수로 — L0 G=0, L1 G=32, L2 G=64.
   R=off&255(위치 램프) 유지, B=(R²+G²)&255 유지.  레벨 내 상수 G 는
   bilinear(레벨 내 4탭)에 불변이므로 MM8S 에서도 **G 가 곧 레벨 혼합 가중**:
   f = (G-32)/32, 분해능 1/32.
2. **런 표 10행**: {MM4S, MM8S} × λt {1.0, 1.25, 1.5, 1.75, 2.0}, 전부
   affine(rhw=1), mapnb=2, fthres 0x10.  m12run 에 tuSpan 필드 추가.
   span = 2^λt / 4 (64텍셀 L0, 16px 다리; python 검증):
   | λt | stride | tuSpan F32 |
   |---|---|---|
   | 1.00 | 2.000 | 0x3F000000 |
   | 1.25 | 2.378 | 0x3F1837F0 |
   | 1.50 | 2.828 | 0x3F3504F3 |
   | 1.75 | 3.364 | 0x3F5744FD |
   | 2.00 | 4.000 | 0x3F800000 |
   (v2 의 nrst/bilin 대조군·dN/dB 는 제거 — G 직접 해독이 대조를 대체하고,
    fetch 실재는 Phase A 로 이미 닫혔다.  persp 는 λ 가 픽셀별 연속이라
    요약 불가 — 이번 질문(가중)에 불요, 제외.)
3. **집계 재정의**: osmgaM12Out → {tag, cnt, gMin, gMax, gSum, bad}.
   센티널 제외, G∉[0,66] 만 bad.  출력 10행(§6 교훈: 16행이면 머리 유실).

### 9-3. 판정 기준 (python 오라클 대조)

| 행 | Mesa 기대 | 개방 조건 |
|---|---|---|
| λt=1.25/1.5/1.75 | G = 40/48/56 (f=frac) | gAvg 오차 ≤ ±2 (f 오차 ≤ 1/16), gMax-gMin ≤ 4 |
| λt=1.0 (정수) | G=32 순수 (f=0) | gAvg-32 ≤ 2.  Phase A 는 "전량 혼합"을 봄 — f 실측이 목적 |
| λt=2.0 (=M 클램프) | G=64 순수 | 64-gAvg ≤ 2 |

- 세 분수점이 통과하고 정수점 f ≤ 1/16 이면 **개방** (Hook 매핑:
  NEAREST_MM_LINEAR→MM4S 0xA, LINEAR_MM_LINEAR→MM8S 0xC, admission 은
  기존 밉 규칙 그대로).
- 정수점에서 f 가 크면(예: ~0.5) **기각-문서화**: fthres 로는 보정 불가
  (min/mag 문턱), λ 바이어스 보정은 MM1S/2S 의 정확한 레벨 산술(§6-2)을
  깨므로 불가.  GLQuake 기본이 MM2S 라 실사용 손실 없음.
- 분수점 가중이 계단(예: 1/4 단위 양자화)이면 오차 한계를 명시해 부분개방
  여부를 별도 판단.

### 9-4. 안전·복구

- 밴드는 기존 M3 하네스 내부(오프스크린 아틀라스, 화면 밖) — 비파괴 원칙
  유지, live 레지스터 재프로그래밍 없음.
- Instance0.table "WARP Triangle Test"=Yes 로 1회 부팅, 측정 후 No 복귀
  (현재 No 상태 확인됨).
- 유저랜드/훅 무변경 — 개방 결정 시 별도 커밋으로.

### 9-5. codex 교차검토 판정 (2026-08-31) — GO-with-changes, 전건 검증 후 채택

| codex 주장 | 검증 | 판정 |
|---|---|---|
| 1/4 격자 λ 점은 1/4 양자화 혼합기를 못 가른다 | python: f 목표 0.25/0.5/0.75 는 1/4 격자와 일치 — 양자화돼도 오라클과 동일 | ✅채택 — eighth-grid 로 교체 |
| eighth-grid tuSpan/기대 G 상수 6점 | python 독립 재계산, 전부 일치 (G 오차 4 로 검출) | ✅채택 |
| MM1S 경계 2행(λ 1.46875/1.53125)으로 λ 산술을 혼합기와 분리 진단 | Mesa COMPUTE_NEAREST_MIPMAP_LEVEL 반올림 확인, 상수 일치 | ✅채택 |
| 판정식이 단방향(gAvg-32≤2 등) | §9-3 원문 확인 — 사실 | ✅채택 — 절댓값·per-pixel 로 교체 |
| 목적지 PW32 라 565 디더 없음, nodither 불요 | .m:8924 MACCESS=PW32, 스펙 :586 32bit 내부 확인 | ✅채택 (nodither 안 건드림) |
| 평균 판정은 공간 오차 상쇄 — per-pixel min/max 필수 | 논리 검증 (36~40 평균 38 통과 예) | ✅채택 |
| 실패 행에만 표본 출력 | syslog 꼬리 16줄 교훈과 정합 | ✅채택 (행당 최대 4표본 1줄) |
| 기각 문구: "불가"가 아니라 "문서화된 GL-의미 보존 보정 없음" (avgstride/rfw/rfh/TMR 검토 포함) | 스펙 9640/9646/9690, DRI mgatex.c:389 avgstride 미사용 확인 | ✅채택 — §9-3 문구 대체 |
| v2 대조군 제거는 진단 회귀력 일부 상실, git 보존으로 충분 | v2 는 커밋 이력에 있음 | ✅채택 |

**확정 런 표 (14행, 전부 affine·mapnb=2·fthres 0x10)**: MM4S/MM8S 각
6행 λt {1.0, 1.125, 1.375, 1.625, 1.875, 2.0} → 기대 G {32,36,44,52,60,64};
MM1S 2행 λt {1.46875→32, 1.53125→64}.
**확정 판정식 (행별)**: cnt==136, bad==0, gMin≥E-2, gMax≤E+2,
|gSum−E·cnt|≤2·cnt.  세 조건군 전부 통과시 개방; 정수점만 실패하면
기각-문서화(위 문구), 분수점 계단이면 오차 한계 명시 후 별도 판단.

## 10. M12-C 실측 (2026-08-31, 1·2차) — 모델 확정, 스펙 명명 재심

14행 전량 회수(2차, 출력 페이싱 후).  **모든 행이 한 모델에 정확히 일치**
(python 대조):

```
λ_hw = e + (m − 1)          (stride = 2^e·m 의 가수-선형 log2 근사)
MM4S: level = round(λ_hw), 레벨 간 혼합 없음   (1.375→L1, 1.625→L2 이진 전환)
MM8S: f = floor(frac(λ_hw)·16)/16 로 두 레벨 혼합 (34/40/48/58 전부 일치)
MM1S: m1-lo/m1-hi 모두 L1 — λ_hw 로는 둘 다 1.5 미만이라 round/floor 미분리
```

Mesa 오라클 대비 최대 편차: MM8S 에서 G 4코드(f 0.125, λ_hw 근사가 지배) —
§9-5 문턱 ±2 초과.  정수점은 전부 순수(Phase A 의 "정수점 전량 혼합"은
tuSpan 정밀도 문제였음이 판명 — 이번 스윕의 1.0/2.0 행이 실측 반증).

**§6-4 재심**: MM4S 무혼합은 "MM4S=NEAREST_MIPMAP_LINEAR" (스펙 9613) 와
모순되고 샘플 수 산술(2탭으로 bilinear 불가)과 함께 **DRI 명명이 옳을
가능성**을 가리킨다.  그 경우 프로덕션 MM2S 매핑(GL_LINEAR_MIPMAP_NEAREST)
은 실제로는 레벨 혼합+포인트 샘플링이 되어 재검이 필요하다.  결정 실험
3행 추가(m2-1.375/m2-1.625: 혼합=DRI 옳음·이진=스펙 옳음, m1-1.625:
λ_hw 1.542 로 MM1S round/floor 분리) — 재부팅 1회.

