# W19 — codex 의 바이너리 근거를 IDA 로 재검증 (2026-08-28)

codex 회신에서 **디스어셈블을 근거로 든 주장**은 지금까지 `objdump` 텍스트 덤프를
눈으로 읽고 받아들였다. 그중 하나(D2-2g 를 보류시킨 것)는 **모드 비트를 독립
확인하지 못한 채 곁가지 논거로만 수용**했다. IDA + Hex-Rays 로 다시 봤다.

대상: `scratch/matrox-win/G400DD32.dll` (PE i386, imagebase `0xbaa00000`).

## 1. D2-2g 를 보류시킨 주장 — **확인. 그리고 더 강해졌다**

`sub_BAA3F9A0` (링 초기화), `0xbaa3fb5d`:

```c
v16 = *(a1 + 64);                     // 링 물리 베이스
v17 = *(*(a1 + 4) + 678);             // MMIO 베이스
*(v17 + 7768) = v16;                  // 0x1E58 PRIMADDRESS  <- 모드 비트 OR 없음
*(v17 + 7772) = v16 | *(a1 + 84);     // 0x1E5C PRIMEND
```

`*(a1 + 84)` 는 앞에서 **2 또는 0 으로만** 설정된다(경로에 따라). 그것은
`PRIMEND` bit1 = **`pagpxfer`** 다. `primnostart`(bit0)는 항상 0.

→ **`PRIMADDRESS` 에 아무 모드 비트도 안 들어간다. `primod = 00` = GENERAL.**
objdump 로는 `mov %ecx,0x1e58(%eax)` 만 보여 `ecx` 의 하위 비트를 알 수 없었다.
디컴파일이 그 출처를 보여 준다.

`sub_BAA3FB90` (제출), `0xbaa3fd3f`:

```c
*(*(*(a1 + 4) + 678) + 7772) = *(a1 + 64) | *(a1 + 84);   // PRIMEND 만
```
**`PRIMADDRESS` 는 여기서 안 쓴다** → `PRIMEND` 만 올려 연장한다. 확인.

그리고 같은 함수가 패킷을 조립하는 방식:

```c
*v9   = HIWORD(a5) | 0x15150000;   // 상위 두 인덱스 슬롯이 DMAPAD
v9[3] = 0x15151515;                // 인덱스 넷 전부 DMAPAD = 순수 패드 패킷
```

`0x15` = `(0x1c54 − 0x1c00) >> 2` = **DMAPAD 인덱스**. 즉 **모든 패킷의 미사용
인덱스 슬롯이 DMAPAD 로 채워진다.** 사양서 4-16 의
*"fill the last set of Pseudo-DMA transfers with no-ops"* 요구 그대로다.

**결론: D2-2g 의 NO-GO 가 약해지지 않고 강해졌다.** 패딩은 곁가지가 아니라
Windows 드라이버가 내보내는 **모든 패킷의 구조**다. VERTEX 모드에는 인덱스
슬롯이 없으므로 이 기법 자체가 존재할 수 없다.

## 2. 출하된 완료 규약의 근거 — **확인, 그리고 하나 더 얻었다**

`sub_BAA56F50`, `0xbaa57146` 부근:

```c
v22 = *(v21 + 7700);                          // 0x1E14 STATUS
v23 = *(v1 + 1306) ? (v22 & 0x40020001) : (v22 & 0x20001);
if ( v23 == 0x20000 ) {                       // ENDPRDMASTS=1, SOFTRAPEN=0
    v25 = *(v24 + 36) + 4;                    // 태그 += 4
    *(v24 + 36) = v25;
    while ( (*(_BYTE *)(v21 + 7696) & 0x1F) < 2 )  // 0x1E10 FIFOSTATUS, 빈 슬롯 >= 2
        ;
    *(v21 + 7252)  = 0;                       // 0x1C54 DMAPAD = 0
    *(v21 + 11340) = v25;                     // 0x2C4C DWGSYNC = 태그
}
```

태그 +4, FIFO 2 슬롯 대기, `DMAPAD` 뒤 `DWGSYNC` — **전부 확인.**

**objdump 로는 안 보이던 것**: Windows 는 이 펜스를
**`(STATUS & 0x20001) == 0x20000`** 일 때만 친다 — 즉 **1 차 DMA 가 이미 끝났고
softrap 이 없을 때만.** 우리 구현은 xfer 폴(`PRIMADDRESS == end && ENDPRDMASTS`)과
`ICLEAR` 로 **그 조건을 구조적으로 만족시킨 뒤** 펜스를 친다.
우연이 아니라 같은 전제였음이 독립적으로 확인됐다.

## 3. 판정

| 재검증 대상 | 결과 |
| --- | --- |
| `PRIMADDRESS` 에 모드 비트 없음 (GENERAL) | ✅ **확인** — objdump 로는 불가능했던 것 |
| `PRIMEND` 만 올려 연장 | ✅ 확인 |
| 미사용 인덱스 슬롯을 `0x15`(DMAPAD)로 패딩 | ✅ 확인, **구조적** |
| `DWGSYNC` 태그 +4 / FIFO 2 / DMAPAD → DWGSYNC / 마스크 | ✅ 확인 |
| 펜스 전 `ENDPRDMASTS=1 && SOFTRAPEN=0` 게이트 | ➕ **새로 얻음** |

**codex 의 바이너리 주장에서 오류는 나오지 않았다.** 다만 §1 은 내가 텍스트
덤프로는 **확인할 수 없었던 것을 수용한** 상태였고, 이제는 확인된 상태다.
그 차이가 이 문서의 이유다.

## 4. 규칙

**앞으로 codex 가 디스어셈블을 근거로 들면 IDA 로 재확인한다.**
`objdump` 텍스트는 레지스터에 실린 **값의 출처**를 보여주지 못한다 —
`mov %ecx,0x1e58(%eax)` 는 `ecx` 가 무엇인지 말하지 않는다.
