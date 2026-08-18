# R2 — Physical Board Profile Evidence Result

상태: **template — physical inspection 결과 아님**

이 file은 operator-approved, power-off/AC-disconnected/ESD-safe inspection 뒤
`R2_PHYSICAL_PROFILE_EVIDENCE.md`로 복사해 실제 evidence만 기록한다. live system의
PCI/MMIO/DDC access, card removal, jumper 변경, ROM flashing은 이 evidence run에
포함하지 않는다.

## Run identity and handling conditions

| field | value |
| --- | --- |
| evidence run ID | `R2-YYYYMMDD-<suffix>` |
| operator / reviewer | pending |
| machine shutdown confirmed | pending |
| AC disconnected confirmed | pending |
| ESD-safe handling confirmed | pending |
| card remained installed / no configuration change | pending |
| serial-number handling | redacted / hash-only / not recorded: pending |

## Board-observed evidence

| ID | source artifact | observed literal marking/layout | readability | report-safe reference |
| --- | --- | --- | --- |
| B1 | board front/back overview | pending | pending | photo orientation/hash: pending |
| B2 | Matrox product/assembly sticker | pending | pending | P/N only; serial redacted |
| B3 | GPU package | pending | pending | package marking: pending |
| B4 | each VRAM package | vendor/part number/count: pending | pending | memory marking list: pending |

사진이 흐리거나 marking 일부가 보이지 않으면 값을 추정하지 않는다. `unreadable`로
기록하고 G2 verdict를 `UNRESOLVED`로 둔다.

## Documentary correlation

| ID | exact source | relationship to observed board | VRAM type/total claim | RAMDAC/pixel-clock claim | independence review |
| --- | --- | --- | --- | --- | --- |
| B5 | primary source URL/document ID: pending | exact P/N or memory part number: pending | pending | pending | pending |
| B6 | independent cross-check URL/document ID: pending | exact P/N or memory part number: pending | pending | pending | pending |

PCI subsystem tuple, `MGA Memory Size=16`, family product guide, catalogue label,
marketplace photo similarity는 B2/B4 identity를 대신할 수 없다. B5와 B6가 같은
catalogue assertion을 재인용하면 independent cross-check가 아니다.

## Resolved profile decision

| field | value | exact supporting evidence | unresolved caveat |
| --- | --- | --- | --- |
| board identity | pending | pending | pending |
| physical VRAM type | pending | pending | pending |
| physical VRAM total | pending | pending | pending |
| applicable main RAMDAC/pixel clock | pending | pending | pending |
| secondary-head relevance | pending | pending | pending |
| G450/G400 family assertion | pending | pending | pending |

## G2 verdict

- `PASS`: B2 or B4 ties the physical target to an exact B5/B6 documentary profile,
  and VRAM type/total plus applicable RAMDAC limit are mutually consistent.
- `UNRESOLVED`: physical marking is absent/unreadable, source is not exact-board
  applicable, or any required value remains inferred.
- `FAIL`: observed board and candidate documentation conflict. Archive the old
  candidate and start R2 from the observed physical identity.

| final verdict | follow-up |
| --- | --- |
| pending | no implementation constants, map length, mode table, or P3 decision change until `PASS` |
