# P1.4 — Read-Only PCI Capability Header Probe

기준일: 2026-08-18

## 목적과 경계

P1.4는 existing P1 probe의 PCI configuration dword read 범위를 standard
capability-list header까지 확장한다. 목적은 MGA function이 VPD capability를
advertise하는지만 확인하는 것이다. VPD contents, BAR, option ROM, VRAM/MMIO,
DAC/PLL/CRTC/engine, DDC, DMA, IRQ, PCI configuration register write에는
접근하지 않는다.

PCI VPD contents를 읽으려면 VPD address/data register transaction이 필요하며,
그 과정에는 device configuration write가 포함될 수 있다. 그러므로 capability
ID `0x03`은 `present` marker만 남기고 data access는 구현하지 않았다.

## 구현

`OpenStepMGAProbe.m`은 existing `pciReadConfigLong()`만 이용한다.

1. status register의 capabilities-list bit를 확인한다.
2. capability pointer (`0x34`)를 read한다.
3. standard capability header의 ID/next byte만 읽는다.
4. offset alignment/range, visited bitmap, 48-hop limit으로 malformed list를
   fail closed 한다.
5. ID `0x03`을 보면 `no-vpd-data-access` marker만 log한다.

`tools/check-p1-config-readonly.sh`은 mapping/device API, PCI config write,
engine/display path, DDC/EDID/I2C/DMA/IRQ 유입을 거부하고, config-address
selector write와 config-data read primitive만 남아 있는지 검사한다.

## Target execution preconditions

- target i386 build가 성공해야 한다.
- NFS `nxlogd`가 실행 중이고 latest log path가 writable이어야 한다.
- unique `/tmp` staging copy를 사용한다. existing `/tmp/OpenStepMGAProbe`는
  삭제·덮어쓰지 않는다. `test/stage-p1-probe.csh`는 existing staging root를
  발견하면 삭제하지 않고 fail closed 한다.
- `run-p1-probe.csh`는 `kl_util -a/-l/-u/-d`로 probe server만 temporary
  lifecycle 처리한다. `driverLoader`/Configure를 호출하지 않고, `MatroxMGA`를
  unload하지 않는다.

## Target verdict

| log result | 의미 | G2 영향 |
| --- | --- | --- |
| `capabilities ... absent` | standard list 없음 | VPD route 없음; G2 미통과 |
| capability list, VPD marker 없음 | VPD advertised 안 됨 | VPD route 없음; G2 미통과 |
| `VPD-capability-present no-vpd-data-access` | future replacement-only VPD design 후보 | contents/type/size는 여전히 미확정 |
| invalid/loop/hop-limit | malformed or unsupported list | follow-up access 금지 |
| load/unload/log failure | P1.4 failure | next hardware step 금지 |

어느 성공 결과도 physical VRAM total/type을 증명하지 않는다. 이 probe는 R2의
board inspection 또는 replacement-only documented identification path를 보조할
수 있는 capability availability evidence일 뿐이다.

Target result는 `P1_P14_CAPABILITY_REPORT.md`에 기록한다. capability IDs
`0x01`, `0x02`만 관찰됐고 VPD ID `0x03`은 없었으므로, VPD contents access를
추가하지 않는다.
