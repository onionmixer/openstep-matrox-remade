# M1.4EC — 호출자가 이미 가지고 있던 그림 (한 방향이던 미러의 나머지 반)

## 결함

OSMesa 에서 호출자가 건네는 버퍼는 **곧 색 버퍼**다. 그 안에 이미 들어 있는 것이
프레임의 출발점이고, 배경 이미지를 실어 넘긴 뒤 그 위에 그리는 것은 이 라이브러리의
평범한 사용법이다(`include/GL/osmesa.h` 의 설명).

이 백엔드는 그 포인터 뒤에 비디오 메모리를 갈아 끼우고 프레임마다 표면을 도로
복사해 준다. 그런데 그 복사가 **한 방향뿐이었다.** 들어오는 쪽이 없으니 호출자의
그림은 그냥 사라진다 — 첫 프레임이 비디오 메모리를 그 위에 덮는다.

`OSMGAMesaBufferMirror` 의 주석은 나가는 쪽만 이야기하고 있었고, 저장소 어디에도
이것이 의도된 절충이라는 기록이 없었다.

## 재현 (`tib`, 신규)

버퍼를 패턴으로 채우고, current 로 만들고, 작은 사각형만 그린 뒤 **사각형 밖**이
그대로인지 묻는다. `glClear` 는 어디에도 없다 — 패턴이 곧 출발 그림이다.

```
고치기 전:
   the first bind keeps it: 10688 pixels outside the quad lost the pattern, 1600 inside were drawn
   FAIL
   a rebind keeps it too  : 10688 pixels outside the quad lost the pattern, 1600 inside were drawn
   FAIL
```

128×96 = 12288, 사각형 1600 을 빼면 10688 — **바깥의 단 한 픽셀도 남지 않았다.**

## 고침

바인딩할 때 한 번, 호출자의 버퍼를 표면으로 들여온다. 미러의 반복문을 방향만
뒤집은 것이고(`dst[y*bufStride + x] = src[y*bufAppRow + x]`), 두 진입 경로 —
첫 바인딩과 같은 크기 재바인딩 — **양쪽 모두**에서 같은 헬퍼를 부른다.

들여온 뒤 표면을 **더럽다고 표시하지 않는다**: 그 순간 양쪽이 일치하므로, 표시하면
방금 들어온 것을 도로 내보내는 미러 한 번을 사는 것뿐이다.

```
고친 뒤:
   the first bind keeps it: 0 pixels outside the quad lost the pattern, 1600 inside were drawn
   ok
   a rebind keeps it too  : 0 pixels outside the quad lost the pattern, 1600 inside were drawn
   ok
```

## 이것이 못 고치는 것

**current 인 채로 호출자가 자기 버퍼를 직접 쓰는 경우**는 아무도 볼 수 없다. 다음
프레임은 더 이상 일치하지 않는 비디오 메모리에서 그린다. 재바인딩이 두 쪽을 다시
맞추는 경계이며, 그렇게 문서화한다.

## codex 판정

| codex 주장 | 내 검증 | 결과 |
| --- | --- | --- |
| 결함은 실재하고, 뒤에서 app→VRAM 을 세우는 곳이 없다 | 두 반환 경로를 직접 읽어 확인했고, **시험이 10688/10688 로 재현**했다 | ✅채택 |
| `OSMesaPixelStore` 쪽은 이미 반대 방향으로 처리되고 있으니 거기에 들여오기를 넣지 말라 | 그 경로는 내보내고 가속을 놓고 앱 버퍼를 되돌린다 | ✅채택 |
| current 인 채의 직접 쓰기는 이 수정으로 못 고친다 — 따로 문서화하거나 별개 결함으로 다뤄라 | 옳다. 코드 주석과 이 문서에 적었다 | ✅채택 |
| 역방향 주소 계산은 맞다 | 미러의 반복문과 정확히 역이다 | ✅채택 |
| 양쪽 반환 경로에서 부르는 헬퍼 하나로 모으고, 부분만 그리는 회귀를 붙여라 | 그렇게 했다. `tib` 가 그 회귀다 | ✅채택 |
| 초기화되지 않은 메모리를 `unsigned long` 으로 읽는 것이 형식적으로 걸린다 | 미러가 반대 방향으로 이미 같은 일을 한다. 이 컴파일러에서 실질적 문제는 아니지만 기록해 둔다 | ⚖️부분 |


---

# M1.4EC(b) — 그리고 깊이는 후퇴를 못 넘고 있었다

## 결함

`OSMesaPixelStore` 가 가속 표면이 갖지 못한 행 길이나 뒤집힌 방향을 요구하면
백엔드는 표면을 중도에 돌려줘야 한다(`osmesa.c:702-709`). 그때 `osmesa_leave_accel`
은 **색은 내보내고**(`OpenStepMesaAccelMirror`) **깊이는 버린다**:
`DepthBuffer = NULL`, 소프트웨어 깊이 재개, `_mesa_alloc_depth_buffer`. 그리고 그
할당자의 주석은 제 손으로 이렇게 적혀 있다 — **"allocate new depth buffer, but
don't initialize it"**.

즉 색은 그 순간을 넘고 깊이는 못 넘는다. GL 어디에도 픽셀 저장 호출이 깊이 버퍼를
비운다는 말은 없다.

## 재현 (`tdp`, 신규)

```
고치기 전:
   depth written while accelerated : 8000
   ok    the surface really was given up
   depth after the fallback        : 0000
   FAIL  the depth code survived
```

### 시험이 스스로 공허함을 잡은 일

첫 판은 `OSMesaPixelStore(OSMESA_ROW_LENGTH, W)` 로 후퇴를 부르려 했는데 **아무 일도
일어나지 않았다**: 이 표면은 디스플레이 stride 가 아니라 **폭**으로 놓인다(그게
공유 깊이의 전제다), 그래서 폭을 요구하는 것은 이미 걸린 값을 요구하는 것이다.
`the surface really was given up` 단언이 그걸 잡았다. 그 단언이 없었다면 뒤의 두
검사는 **한 번도 후퇴하지 않은 표면**에게 질문하고 통과했을 것이다. 방향 뒤집기가
실제로 다른 조건이다.

## 고침

`_mesa_alloc_depth_buffer` 직후, 표면을 반납하기 **전에** — 그 지점이 둘 다 살아
있는 마지막 순간이다 — 공유 깊이를 새 소프트웨어 버퍼로 복사한다. 백엔드 쪽은
`OpenStepMesaAccelCopyDepth` 하나이고, 16비트이며 stride 가 폭과 같은 경우만
받는다(그게 애초에 깊이 매핑을 내주는 전제이고, 그래야 Mesa 의 Width×Height 배치와
같은 배치가 된다). 아니면 0 을 돌려주고 호출자는 예전과 똑같은 상태가 된다.

```
고친 뒤:
   depth written while accelerated : 8000
   ok    the surface really was given up
   depth after the fallback        : 8000
   ok    the depth code survived
   ok    a farther quad is still rejected
```

## codex 판정

| codex 주장 | 내 검증 | 결과 |
| --- | --- | --- |
| 순서는 안전하다 — 반납이 마지막이라 재할당 시점에 매핑이 살아 있다 | `osmesa_leave_accel` 전문을 읽어 확인 | ✅채택 |
| `GLdepth` 는 `GLuint` 지만 16비트 비주얼에서는 할당이 `GLushort` 이고 배치는 Width×Height 무패딩 | `types.h:84`, `depth.c` 의 할당·주소 계산 확인 | ✅채택 |
| 거절된 재바인딩도 같은 경로를 지나므로 한 번의 수정으로 덮인다 | 그렇다 | ✅채택 |
| 파괴·fork 는 이 수정 대상이 아니다 | 파괴는 보존할 이유가 없고, fork 는 프로브가 매핑을 먼저 놓아 복사할 원본이 없다 | ✅채택 |
| 스텐실·누산은 같은 모양의 손실이 없다 | 가속은 `DepthBuffer` 만 바꾼다 | ✅채택 |
| VRAM 을 **직접 읽지 말고** 드라이버 중재 복사를 쓰라 | ⚖️부분: 근거는 있으나 이 경우엔 반증돼 있다. 64바이트 정체 구간은 매핑 창 앞부분이고 깊이는 색 표면 뒤 페이지 경계에 있으며, `tdm`·`tdc`·`depth-agree` 가 finish 직후 같은 매핑을 직접 읽어 엔진이 쓴 값을 얻는다. 조건 검사(16비트·stride==폭·크기 일치)는 요구대로 넣었다 | ⚖️부분 |
| `glResizeBuffersMESA` 는 가속 깊이를 놓지 않아 낡은 치수가 남을 수 있다 | 별개 항목으로 기록 — 이 백엔드에서 그 확장이 닿는지부터 봐야 한다 | ⏭️미검증 (기록) |
