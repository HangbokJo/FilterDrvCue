# 1단계(로깅 필터) 검증 절차

대상: `CueMidiFilter` 1단계. 통과 + 설정 디스크립터 로깅만 하며 응답은 수정하지 않는다.
테스트 시스템(물리 PC 권장, 테스트 서명 + 커널 디버거)이 준비된 뒤 수행한다. 이 개발 PC에는 설치하지 않는다.

## 준비
- [ ] 테스트 PC: 테스트 서명 모드 켬 (`bcdedit /set testsigning on`, 재부팅), 커널 디버거 연결(WinDbg, 예: 네트워크)
- [ ] 디버거에서 `ed nt!Kd_IHVDRIVER_Mask 0xF`(DbgPrintEx 출력 확인용, 필요 시)
- [ ] Driver Verifier 켬: `CueMidiFilter.sys`에 특수 풀, IRQL 검사, DDI 준수, 강제 IRP 로깅. KMDF Verifier도 확인
- [ ] 장치의 현재 상태 기록: 장치 관리자 `MI_00`(정상), `MI_03`(코드 10, `C0000182`), 하드웨어 ID/호환 ID 캡처
- [ ] 패키지 테스트 서명: `x64\Debug\CueMidiFilter\` (`.sys`, `.inf`, `.cat`)

## 설치와 로그 수집
1. 확장 INF 설치 (`pnputil /add-driver CueMidiFilter.inf /install`)
2. `MI_03`의 장치 스택에 `CueMidiFilter`가 하위 필터로 붙었는지 확인 (`!devstack` 또는 장치 관리자 드라이버 세부 정보)
3. `MI_03`을 비활성화 → 활성화하거나 장치를 재연결해 열거를 다시 일으킨다
4. 디버거 출력에서 `CueMidiFilter:` 로그를 수집한다

## 기대 로그와 확인할 것
| 확인 항목 | 로그에서 볼 것 |
|---|---|
| 요청 경로 | `config descriptor request: function=0x....` (0x0B GET_DESCRIPTOR_FROM_DEVICE 또는 0x08/0x32 CONTROL_TRANSFER) |
| 선행(9바이트) 요청 여부 | `received=9 (partial)` 후 `received=<wTotalLength> (complete)` |
| 자식 디스크립터 형태 | hex 덤프에서 인터페이스 번호(IF 0–5 전체인지 IF 3–4만인지), IF 4 포함 여부, `baInterfaceNr` 값 |
| 다른 경로 사용 여부 | `URB function` 로그 중 GET_DESCRIPTOR 계열이 있는지, 상세(`descriptor type`, `setup`) |
| 실패 | `config descriptor request failed`, `buffer is not accessible` |

로그를 `docs/`에 저장하고 분석 문서의 미해결 항목(IF 3–4 포함 여부, 전체/필터링 형태)을 갱신한다.

## 안정성 확인 (Verifier 켠 상태)
- [ ] 설치·제거·재설치 후 블루스크린/Verifier 위반 없음
- [ ] `MI_03` 비활성화·활성화, 케이블 제거·재연결, 절전 진입·복귀, 서프라이즈 제거 정상
- [ ] 선택적 일시 중단(selective suspend)에서 요청이 실패하지 않는지 (로그에 비정상 상태 코드 없음)
- [ ] `MI_00` 오디오 재생·녹음이 설치 전과 동일하다 (필터가 붙지 않았는지 `!devstack`으로 확인)
- [ ] MIDI 트래픽이 있어도 요청 로그가 한도(64건)에서 멈춘다

## 제거
`pnputil /delete-driver oemNN.inf /uninstall`, 재부팅 후 `MI_03` 스택에서 필터가 사라졌는지 확인한다.

## 결과 기록
수행하지 못한 항목은 이유와 함께 이 문서 아래에 기록한다.
