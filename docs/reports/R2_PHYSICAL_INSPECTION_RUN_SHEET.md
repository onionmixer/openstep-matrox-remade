# R2 — Physical Board Evidence Collection Run Sheet

상태: **template only — 실행하지 않음**  
기준일: 2026-08-18

## 목적

현 target의 G450 PCI board에 직접 적용되는 board identity, physical VRAM
type/total, RAMDAC limit evidence를 수집한다. 이 sheet의 목적은 card를
동작시키거나 register를 읽는 것이 아니라, 이미 장착된 board의 immutable
label/marking/photograph를 clean evidence로 보존하는 것이다.

## 사전 조건과 금지 사항

- operator가 chassis opening 또는 high-resolution photograph 촬영을 승인해야 한다.
- machine은 정상 shutdown, power-off, AC disconnect, ESD-safe 상태에서만 physical
  label을 확인한다. live system에서 card를 만지거나 뽑지 않는다.
- card removal, jumpers 변경, ROM flashing, monitor/driver configuration 변경은
  이 sheet 범위 밖이다.
- 이 작업은 OPENSTEP의 `MatroxMGA`, `driverLoader`, PCI configuration, VRAM/MMIO,
  DDC/EDID, Mesa/P3 상태를 전혀 바꾸지 않는다.

## 수집 항목

| evidence ID | 필요한 원본 | 기록할 값 | 허용 해석 |
| --- | --- | --- | --- |
| B1 | board front/back 전체 사진 | connector layout, PCB revision, stickers | exact product/board revision candidate 식별 |
| B2 | Matrox product/assembly sticker 근접 사진 | P/N, assembly number, serial (공개 report에는 redact) | official product document과 model-level 대조 |
| B3 | GPU package marking | MGA chip marking/revision | G400/G450 family assertion cross-check |
| B4 | 모든 memory package의 readable marking | vendor, part number, package count | memory datasheet와 대조할 candidate 제공 |
| B5 | board-specific official datasheet/service inventory | exact P/N에 대응하는 memory type/total, RAMDAC information | G2 primary evidence 후보 |
| B6 | B5와 독립적인 source | 동일 P/N/board revision의 type/total/clock | B5 cross-check |

photo마다 evidence ID, 촬영 면(front/back), orientation, focus status만 report에
기록한다. serial number는 source archive에 보관할 필요가 없으며 report에는
hash/redaction 여부만 적는다.

## 판정 규칙

G2를 통과하려면 B2 또는 그에 동등한 board identity가 B5/B6의 exact product
reference와 일치하고, 두 source가 현 board의 VRAM **type과 total**을 일관되게
명시해야 한다. RAMDAC limit도 primary source 또는 chip-specific documentation으로
현 board에 적용됨을 보여야 한다.

다음은 불충분하다.

- `102b:0525` 또는 `102b:0d43` PCI ID/catalogue label만 있는 경우
- OPENSTEP `MGA Memory Size=16` configuration profile만 있는 경우
- G450 product family의 “16 or 32 MB DDR” table만 있는 경우
- memory package가 보이지만 part marking을 판독하지 못했거나, part number를
  memory size/type datasheet와 연결하지 못한 경우
- online marketplace/photo의 외형 유사성만 있는 경우

## 결과 기록 template

| field | value |
| --- | --- |
| evidence run ID | `R2-YYYYMMDD-` |
| B1 photo/hash | pending |
| B2 exact board P/N | pending |
| B3 GPU marking | pending |
| B4 memory part numbers/count | pending |
| B5 primary source URL/document ID | pending |
| B6 independent cross-check | pending |
| resolved VRAM type | pending |
| resolved physical VRAM total | pending |
| resolved RAMDAC limit | pending |
| G2 verdict | pending |

G2가 통과하지 않으면 이 sheet는 `UNRESOLVED`로 남긴다. family-level value나
가장 큰 plausible memory size를 fallback으로 채우지 않는다.

실제 evidence result의 field/acceptance format은
`R2_PHYSICAL_PROFILE_EVIDENCE_TEMPLATE.md`를 사용한다.
