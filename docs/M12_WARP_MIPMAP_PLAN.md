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
