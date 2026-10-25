# MI_03 MIDI 디스크립터 보정 필터

근거 문서: `docs/SPARTAN CUE USB MIDI 오류 원인 분석.md`

## 목표
SPARTAN CUE(`USB\VID_2360&PID_7011`)의 MIDI function(`MI_03`)이 `usbaudio.sys` 시작 단계에서 `C0000182`(코드 10)로 실패한다. 유력 원인은 UAC1 AudioControl 헤더의 `baInterfaceNr`가 MIDI 스트리밍 인터페이스(4)가 아니라 1을 가리키는 펌웨어 결함이다. 펌웨어는 수정할 수 없으므로, `MI_03` 스택의 하위 필터가 설정 디스크립터 응답을 보정해 `usbaudio.sys`가 정상 디스크립터를 보게 한다.

## 범위
- 포함: 설정 디스크립터 응답 로깅(1단계), `baInterfaceNr` 보정(2단계), 확장 INF, 서명·배포 절차
- 제외: 오디오 function(`MI_00`) 변경, DFU(`MI_05`), 펌웨어 수정, 사용자 모드 앱/IOCTL, MIDI 데이터 경로 가로채기, 원인이 다른 결함으로 밝혀진 경우의 추가 보정(별도 설계)

## 장치 스택
- 필터 위치: **하위(lower) 필터, 인터페이스 노드 `USB\VID_2360&PID_7011&REV_0651&MI_03`**
  - 스택: `usbaudio.sys`(FDO) → **CueMidiFilter** → usbccgp가 만든 PDO
  - 상위 필터는 `usbaudio.sys`가 아래로 보내는 요청을 못 보므로 부적합
- 연동 드라이버: `usbaudio.sys`(Windows 기본, MIDI function), `usbccgp.sys`(복합 장치 부모)
- 가로채는 요청: `IRP_MJ_INTERNAL_DEVICE_CONTROL` / `IOCTL_INTERNAL_USB_SUBMIT_URB` 중 설정 디스크립터 조회(`URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE`, `USB_CONFIGURATION_DESCRIPTOR_TYPE`). 그 외 모든 요청은 그대로 전달
- 확인 필요(1단계 로그로 확정): 요청이 이 URB 형태로 오는지(`URB_FUNCTION_CONTROL_TRANSFER[_EX]` 가능성), usbccgp가 자식에게 주는 디스크립터가 IF 3–4만 필터링한 것인지 설정 전체인지, IF 4가 들어 있고 인터페이스 번호가 원본 그대로인지 (보정 규칙은 어느 경우든 대응)

## 설계
**파일**
- `src/Driver.cpp`: `DriverEntry`(`extern "C"`), `EvtDeviceAdd`(필터 모드 초기화, 큐 생성, 레지스트리 읽기)
- `src/Filter.cpp`: `EvtIoInternalDeviceControl`, 완료 루틴
- `src/DescriptorPatch.cpp/.h`: 디스크립터 탐색·보정 로직. 하드웨어와 무관한 순수 함수로 두어 호스트에서 단위 테스트 가능
- `src/Trace.h`: `DbgPrintEx` 기반 로그와 hex 덤프 헬퍼
- `inf/CueMidiFilter.inx`: 확장 INF

**구조체**
- `DEVICE_CONTEXT`: `BOOLEAN PatchEnabled`(레지스트리 `PatchEnabled`, 기본 0), `LONG PatchCount`, `LONG DumpCount`

**요청 흐름**
1. `EvtIoInternalDeviceControl` 수신 (병렬 큐, `WdfIoQueueDispatchParallel`)
2. `IoControlCode != IOCTL_INTERNAL_USB_SUBMIT_URB`이면 그대로 전달(send-and-forget)
3. URB가 설정 디스크립터 조회가 아니면 그대로 전달
4. 맞으면 완료 루틴을 등록하고 전달
5. 완료 루틴: 성공이고 `TransferBufferLength >= wTotalLength`일 때만 처리. 버퍼는 `TransferBuffer` 또는 `MmGetSystemAddressForMdlSafe`로 얻는다. 1단계는 덤프만, 2단계는 `PatchEnabled`가 켜졌을 때 보정
6. 요청은 항상 원래 상태 그대로 완료(우리가 상태를 바꾸지 않음)

**보정 규칙** (`DescriptorPatch`) — 아래를 모두 만족할 때만 1바이트 수정, 아니면 무변경
- 디스크립터를 `bLength`로 순회하며 UAC1 AudioControl 인터페이스(class 0x01, subclass 0x01, protocol 0x00) 뒤의 `CS_INTERFACE` 헤더(`09 24 01`, `bcdADC` 0x0100)를 찾는다. UAC2 AudioControl(IF 0, protocol 0x20)은 대상이 아니다
- `bInCollection == 1`이고 `baInterfaceNr[0]`이 가리키는 인터페이스가 MIDIStreaming(class 0x01, subclass 0x03)이 **아니다**. 가리키는 인터페이스가 버퍼에 없는 경우도 "아님"으로 본다 (이 장치는 IF 1, 서브클래스 0x02)
- 이 AudioControl 인터페이스 뒤부터 다음 AudioControl 인터페이스 또는 설정 끝까지 MIDIStreaming 인터페이스(서로 다른 `bInterfaceNumber` 기준)가 **정확히 하나** 있다 (이 장치는 IF 4)
- 위를 모두 만족하면 `baInterfaceNr[0]`을 그 MIDIStreaming 인터페이스 번호로 바꾼다 (값 4를 하드코딩하지 않음)
- usbccgp가 자식에게 IF 3–4만 필터링해 주든 설정 전체(IF 0–5)를 주든 같은 규칙으로 동작한다
- 이미 정상이거나 조건이 안 맞으면 로그만 남기고 통과 → 펌웨어가 고쳐져도 무해, 같은 응답에 두 번 적용해도 안전

**IRQL·락**: 완료 루틴은 최대 `DISPATCH_LEVEL`이라 페이지 가능 코드·메모리·`DbgPrintEx` 과다 출력 금지(덤프는 시작당 1회로 제한). 카운터는 `InterlockedIncrement`, 별도 락 없음.

**PnP·전원**: 하위 필터이며 전원 정책 소유자가 아니다. 별도 콜백 없이 WDF 기본 통과 처리에 맡긴다. 제거·절전·비활성화 때 보유 자원은 없다. 장치 재시작 때마다 설정 디스크립터 조회가 다시 보정된다.

**사용자 모드 인터페이스**: 없음(IOCTL, 장치 인터페이스 노출 안 함). 설정은 `HKR` 값 `PatchEnabled`(REG_DWORD)만 사용하며 INF가 기본값을 넣는다.

**INF·서명·배포**
- 확장 INF(Extension 클래스 `{e2f84ce7-8efa-411c-aa69-97454ca4cb57}`)로 기존 usbaudio 설치를 건드리지 않고 `LowerFilters`(MULTI_SZ append)와 서비스(`CueMidiFilter`, 수동 시작)를 추가
- 매칭 ID는 `USB\VID_2360&PID_7011&REV_0651&MI_03`로 한정(`bcdDevice` 0x0651 제한을 INF 수준에서 처리). 다른 펌웨어 버전은 별도 검토
- 서명: 회사 EV 인증서로 **Partner Center(Windows Hardware Dev Center)에 제출하는 CAB을 서명**하고, 그 결과로 Microsoft 서명(attestation)을 받는다. EV 인증서로 직접 서명한 드라이버만으로는 Windows 11 일반 PC에서 로드되지 않는다. 계정 권한은 회사에서 확인 필요
- 개발 중에는 테스트 PC에서 테스트 서명 모드 사용

## 대안과 선택 이유
| 대안 | 결론 |
|---|---|
| 펌웨어 수정 | 근본 해결이지만 불가 |
| 복합 장치(`USB\VID_2360&PID_7011`) 하위 필터 | 잘 동작하는 오디오까지 필터를 통과시켜 장애 시 영향이 큼 → 제외 |
| usbccgp 열거 콜백 필터 | 인터페이스 묶음만 바꿀 수 있어 이번 원인에 맞지 않음 |
| `MI_03`에 WinUSB + 사용자 모드 MIDI | 호환성이 낮고 규모가 큼 → 필터가 실패하면 대안으로 재검토 |
| **`MI_03` 하위 필터 보정(채택)** | 영향 범위가 MIDI function 하나로 한정 |

## 위험 요소
- **원인 불확실**: `C0000182`가 `baInterfaceNr` 외의 결함이면 보정이 효과가 없다 → 1단계 로그와 정상 MIDI 장치 디스크립터 대조로 먼저 확인
- **usbaudio.sys가 다른 경로로 디스크립터를 읽을 가능성**(URB 외 경로): 1단계에서 확인, 불가능하면 방안 재설계
- **블루스크린**: 완료 루틴의 버퍼 접근, 부분 응답, 요청 이중 완료 → 방어적 길이 검사, Driver Verifier로 검증
- **MIDI 장치 열거 지연·실패**: 필터 오류로 `MI_03`가 못 올라올 수 있음 → 해당 function에 한정됨(오디오는 별개 스택)
- **서명 지연**: Partner Center 제출·승인 시간이 일정에 영향
- **배포 후 펌웨어 업데이트**: 새 펌웨어가 나오면 `bcdDevice`가 바뀌어 필터가 매칭되지 않을 수 있음(의도된 동작)

## 작업 순서
1. 프로젝트 골격 생성(KMDF 필터 템플릿, 빌드·분석 설정) — `CLAUDE.md` 명령어 갱신
2. **1단계**: 통과 + 설정 디스크립터 hex 덤프 → 테스트 PC에서 로그 수집
3. 로그로 가설 확정(요청 경로, IF 4 포함 여부, 인터페이스 번호). 틀리면 설계 수정 후 재승인
4. **2단계**: `DescriptorPatch`와 호스트 단위 테스트, 보정 적용
5. `/test-plan`, `reviewer` 리뷰
6. Partner Center 제출·서명, 설치 패키지 검증

## 완료 기준
- 경고 0 빌드, 코드 분석·`infverif` 통과
- `DescriptorPatch` 호스트 테스트 통과: 실제 Config 1 덤프(`docs/USB descriptor.txt` 기준, 전체 IF 0–5), `MI_03` 형태(IF 3–4만) 입력, 이미 정상인 입력, IF 4 없는 입력, MIDIStreaming이 둘 이상인 입력, 잘린 버퍼, 중복 적용
- 테스트 PC(Driver Verifier 켠 상태)에서 `MI_03` 코드 10 해소, MIDI 포트 표시, 루프백·대용량 SysEx·장시간 전송 통과
- 케이블 분리·재연결, 절전 복귀, 비활성화·활성화, 제거 후 재설치 정상
- `MI_00` 오디오 재생·녹음 영향 없음
- 확인하지 못한 항목은 보고서에 명시
