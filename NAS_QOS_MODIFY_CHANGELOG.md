# UE NAS 기반 동적 5QI/QoS 변경 작업 정리

## 1) 목표

- 기존 SMF REST `qos-modify` 중심 제어에서, **UE NAS (PDU Session Modification Request)** 경로로 QoS 변경을 수행.
- 목표 경로:
  - `UE NAS -> gNB(RRC/NGAP) -> AMF -> SMF -> AMF -> gNB(CU-CP/CU-UP) -> scheduler`
- 트래픽 생성(`ip netns`, `iperf3`)은 사용자면(UP) 검증용이며, NAS 제어 메시지 전송은 UE 제어 소켓으로 분리.

## 2) 핵심 아키텍처/동작 요약

- UE는 AF_UNIX datagram 소켓으로 `MODIFY <psi> <qfi> <5qi> <gbr_dl> <gbr_ul> <mbr_dl> <mbr_ul>` 명령 수신.
- 명령을 UE NAS 5G 스택이 `PDU Session Modification Request`로 인코딩하여 UL NAS transport로 송신.
- 코어(Open5GS SMF)에서 NAS QoS Flow Description 파싱 및 NGAP QoS Modify 빌드.
- gNB CU-CP가 DRB modify item 생성, CU-UP이 QFI->5QI remap을 DRB context에 반영.

## 3) 파일별 코드 변경 사항

## 3.1 `srsRAN_4G` (UE)

### `srsue/src/stack/upper/nas_5g.cc`

- `send_pdu_session_modification_request(...)` 구현 추가.
- QoS flow description 빌드 헬퍼 추가(`build_modify_qos_flow_descriptions`).
- NAS PTI 관리 버그 수정:
  - `allocate_next_proc_trans_id()` 동작 수정.
  - `release_proc_trans_id()`의 index 처리(1-based/0-based) 정합화.
- PDU session 상태 검사/조회 로직 보강:
  - `configured && established` 의존 제거, `established` 기준으로 조회.
  - 세션 테이블 dump 로그 추가(실패 시 원인 추적).
- range-for pass-by-value 버그 수정:
  - `for (auto pdu_session : pdu_sessions)` -> `for (auto& pdu_session : pdu_sessions)`
  - `init_pdu_sessions`, `configure_pdu_session`, `unestablished_pdu_sessions`,
    `get_unestablished_pdu_session`, `reset_pdu_sessions` 등 적용.
- 세션 테이블 누락 시 best-effort 삽입/보정 로직 추가.
- 주요 로그 추가:
  - `No NAS security context...`
  - `PDU session ... not established locally...`
  - `Sending PDU Session Modification Request in UL NAS transport (PSI, QFI, 5QI)`

### `srsue/hdr/stack/upper/nas_5g.h`

- `send_pdu_session_modification_request(...)` 선언 추가.

### `srsue/hdr/stack/upper/nas_config.h`

- `nas_5g_args_t`에 `control_socket` 필드 추가.

### `srsue/src/stack/ue_stack_lte.cc`

- NAS 5G control socket 통합:
  - `open_nas5g_control_socket()`
  - `close_nas5g_control_socket()`
  - `poll_nas5g_control_socket()`
- `init()`에서 SA 모드 + socket 설정 시 소켓 open.
- `stop_impl()`에서 소켓 close/unlink.
- `run_tti_impl()`에서 소켓 polling 수행.
- `MODIFY ...` command 파싱 후 NAS modify 함수 호출.
- 포맷 불일치 시 경고 로그:
  - `Ignored control datagram (expected MODIFY ...)`

### `srsue/hdr/stack/ue_stack_lte.h`

- control socket 관련 멤버/메서드 선언 추가.

### `srsue/src/main.cc`

- CLI 옵션 추가:
  - `--nas.5g_control_socket`

### `srsue/ue.conf.example`

- `5g_control_socket` 사용 예시 추가.
- `socat`로 MODIFY 명령 전송 예시 추가.

## 3.2 `Open5gs_CORE` (SMF)

### `src/smf/gsm-handler.c`

- `gsm_handle_pdu_session_modification_qos_flow_descriptions()` 보강:
  - NAS 5QI 파라미터를 `qos_flow->qos.index`로 반영.
  - 파라미터 인덱스 로깅 버그(`param[i]` vs `param[j]`) 수정.
- GBR/MBR/5QI 파싱 관련 `ogs_info` 로그 추가.
- PFCP modify 플래그 반영(`OGS_PFCP_MODIFY_QOS_MODIFY`) 확인.

## 3.3 `srsRAN_CORE` (gNB CU-CP/CU-UP)

### `lib/cu_cp/routines/pdu_session_routine_helpers.cpp`

- DRB modify item 빌드 추적 로그 추가:
  - `[QoS-MODIFY] [CP-5QI] Building DRB modify item...`
  - `Add modify flow_map_info...`
  - `Built DRB modify item complete...`

### `lib/cu_up/pdu_session_manager_impl.cpp`

- `modify_pdu_session()` 내 QoS flow 적용 TODO 구간 구현:
  - `drb_to_mod.flow_map_info` 기반으로 `drb->qos_flows` 갱신.
  - 신규 QFI mapping 없으면 생성, 있으면 `five_qi` 업데이트.
- 상세 추적 로그 추가:
  - 요청 flow/현재 flow 덤프
  - `Updated flow mapping in DRB context ...`
  - dynamic/invalid/unconfigured 5QI 경고
- **CU-UP 최초 유입 시점 로그 추가**:
  - `[QoS-MODIFY] [CU-UP-INGRESS] Received PDU Session Modify request ...`
  - 이 로그는 local state lookup/update 전에 출력됨.

## 3.4 테스트 스크립트

### `scripts/iperf3_dynamic_5qi_test.sh`

- SMF REST 기반 변경 로직 제거, UE NAS control path만 사용.
- UL 제거, **DL-only** 시나리오로 정리.
- 소켓 전송 함수(`change_5qi_ue0`)에서:
  - `socat` 전송 실패 시 에러 출력
  - `Permission denied` 시 `sudo` 재시도 로직
- 시나리오 값 반영:
  - Best-effort: `5QI=9`, GBR 없음
  - Sensor streaming: `5QI=3`, GBR 20 Mbps
  - Emergency braking: `5QI=80`, GBR 없음
  - Remote control: `5QI=84`, GBR 15 Mbps
- 단계별 verify 함수로 UE/SMF 로그 propagation 확인.

## 4) 주요 장애와 해결

- 소켓 파일 미생성:
  - 원인: UE 프로세스 종료로 unlink 발생 / 잘못된 파일 위치 빌드.
  - 조치: 실행 바이너리-소스 정합, UE 상시 실행 유지.
- 소켓 권한 문제(`Permission denied`):
  - 원인: root 소유 소켓.
  - 조치: `sudo socat` 재시도 또는 소켓 권한 조정.
- 링크 에러(`undefined reference ... send_pdu_session_modification_request`):
  - 원인: 구현 누락/오배치(`hdr` 경로의 잘못된 `.cc`).
  - 조치: 올바른 `src/.../nas_5g.cc` 구현 후 clean rebuild.
- UE에서 `not established`:
  - 원인: range-for pass-by-value로 established 상태 반영 실패.
  - 조치: reference loop로 수정 + 누락 세션 엔트리 보완.
- CU-UP에서 `req_flow_count=0`:
  - 원인: modify flow 적용 로직 미구현(TODO).
  - 조치: DRB context QoS flow update 로직 구현.

## 5) 검증용 로그 포인트

- UE NAS:
  - `Sending PDU Session Modification Request in UL NAS transport`
  - `No NAS security context`
  - `not established locally`
- SMF:
  - `NGAP-BUILD`
  - `QFI=`, `5QI=`, `GBR`, `MBR`
- gNB CU-CP/CU-UP:
  - `[CP-5QI] Building DRB modify item`
  - `[CU-UP-INGRESS] Received PDU Session Modify request`
  - `Requested flow from control-plane`
  - `Updated flow mapping in DRB context`
- Scheduler/GTPU:
  - remap 이후 first-packet / first-grant 로그에서 5QI 전환 확인.

## 6) 권장 grep 예시

```bash
# UE NAS
grep -E "MODIFY|Sending PDU Session Modification Request|not established|No NAS security context" /tmp/ue1.log

# SMF
grep -E "qos_flow_descriptions|NGAP-BUILD|QFI=|5QI=|GBR|MBR" ~/srsRAN_main/open5gs/logs/smfd.log

# gNB CU-CP/CU-UP
grep -E "CU-UP-INGRESS|QoS-MODIFY|CP-5QI|Requested flow from control-plane|Updated flow mapping|req_flow_count" /tmp/gnb.log
```

## 7) 현재 상태 요약

- UE NAS trigger -> AMF/SMF -> CU-CP -> CU-UP -> scheduler 경로에서
  - 5QI 전환(예: 9 -> 3 -> 80 -> 84) 로그 확인됨.
  - CU-UP DRB context remap update 로그 확인됨.
  - 스케줄러 first-grant 로그에서 변경된 5QI 반영 확인됨.
- 즉, 초기 이슈였던 "NAS는 갔는데 CU-UP에 반영 안 됨" 상태는 코드 수정으로 해소됨.

