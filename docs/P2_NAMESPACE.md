# P2 — MiG Namespace Reservation

상태: **preflight passed (2026-08-18)**

## MiG subsystem

`openstep_mga`의 subsystem ID 후보는 `4100`이다.

- 로컬 `ref/openstep/headers`의 installed Mach, kernserv, servers `.defs`를
  전수 검색했다. 사용된 subsystem ID는 64, 100, 200, 400, 1040, 4241775이며
  4100은 없다.
- workspace의 추가 `.defs`도 검색했으며 OpenStepMGA 이름이나 4100 사용은
  없었다.
- 이는 local source/included SDK 충돌 검사를 통과했다는 뜻일 뿐, target에
  설치된 모든 third-party binary의 runtime message ID를 열거한 것은 아니다.
  P2 skeleton은 first load 전에 only-control request 하나로 dispatch를
  검증하고, unknown message나 collision 징후가 있으면 즉시 unload한다.

## Advertised port name

후보 service name은 `openstepmga0`이다. `SMAP`과 `ADVERTISE`에는 이 정확한
ASCII 이름을 사용한다.

`test/openstep-mga-namecheck.c`는 NextDev Mach 문서의
`netname_look_up(name_server_port, "", name, &port)` 절차를 사용해 local name
space만 read-only로 조회한다.

- `NETNAME_NOT_CHECKED_IN`: 이름을 사용할 수 있는 precondition.
- `KERN_SUCCESS`: 이름 충돌; source/load command를 바꾸고 다시 검사한다.
- 그 밖의 return: name service 또는 client build 문제이므로 service build/load를
  시작하지 않는다.

이 검사 자체는 port allocate/check-in/check-out을 하지 않으며, MGA card나
existing `MatroxMGA` driver를 건드리지 않는다.

실기 결과는 `OPENSTEP_MGA_NAMECHECK name=openstepmga0 result=1001 port=0`이며,
`NETNAME_NOT_CHECKED_IN`을 반환했다. 이후 P2.0 `SMAP`/`ADVERTISE` smoke test가
동일 이름으로 lazy-load와 RPC를 성공했다. 실행 기록은
`P2_P20_PROTOCOL_REPORT.md`에 있다.
