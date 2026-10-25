# SPARTAN CUE USB MIDI 오류 원인 분석

Oct 25, 2026 · @jogerse

## 요약

MIDI function(`MI_03`)은 Windows 기본 드라이버(usbaudio.sys)가 로드되지만, 장치 구성 오류(`C0000182`, 코드 10)로 시작에 실패한다. 가장 유력한 원인은 MIDI용 AudioControl 인터페이스(IF 3) 헤더의 `baInterfaceNr`가 MIDI 스트리밍 인터페이스(4)가 아니라 재생 스트림 인터페이스(1)를 가리키는 펌웨어 디스크립터 결함이다.

- 증상: UAC2 오디오는 정상, USB MIDI만 동작하지 않음
- 판단: 드라이버 매칭 문제가 아니라 디스크립터 검증 실패
- 권장 조치: 펌웨어에서 `baInterfaceNr`를 `0x01` → `0x04`로 수정 (1바이트, 길이 변화 없음)
- 상태: 가설 단계. 디스크립터 덤프(`docs/USB descriptor.txt`)로 `baInterfaceNr` 결함은 사실로 확인됐으나, 이것이 `C0000182`의 원인이라는 인과는 미확인
- 제약: 펌웨어는 수정할 수 없음(2026-10-25 확인). `MI_03` 하위 필터 드라이버로 디스크립터를 보정하는 방향으로 진행 (`docs/design/midi-descriptor-filter.md`)

## 대상 장치와 환경

| 항목 | 값 |
| --- | --- |
| 제품 | SPARTAN CUE USB Audio 2.0 (AUDIOPROBE) |
| VID / PID | 0x2360 / 0x7011 |
| bcdDevice | 0x0651 |
| 속도 | High-speed (USB 2.0) |
| 장치 클래스 | 0xEF / 0x02 / 0x01 (IAD 사용 복합 장치) |
| 구성 수 | 2 (분석은 Config 1 기준) |
| iSerialNumber | 없음 (0x00) |
| 개발 환경 | Visual Studio 2026 + WDK |
| 오디오 드라이버 | usbaudio2.sys (Windows 기본, 정상 동작) |
| MIDI 드라이버 | usbaudio.sys (Windows 기본, 시작 실패) |

## 디스크립터 분석

Config 1은 인터페이스 6개로, UAC2 오디오 function 1개, MIDI 1.0 function 1개, DFU 1개로 구성된다. MIDI 인터페이스(IF 3–4)는 어떤 IAD에도 속하지 않는다.

| 인터페이스 | 기능 | 내용 |
| --- | --- | --- |
| IAD #1 (IF 0–2) | UAC2 오디오 | 2in / 2out |
| IF 0 | AudioControl (UAC2) | Clock Source 0x29 → Clock Selector 0x28(입력 1개). 재생: IT 0x02 → FU 0x0A → OT 0x14. 녹음: IT 0x01 → FU 0x0B → OT 0x16 |
| IF 1 | 재생 스트림 | Alt1 24bit(4B 슬롯), Alt2 16bit(2B 슬롯). 비동기 iso OUT EP 0x01 + 명시적 피드백 EP 0x81 |
| IF 2 | 녹음 스트림 | Alt1 24bit(4B 슬롯). 비동기 iso IN EP 0x82 |
| IF 3 | AudioControl (UAC1) | MIDI용. 헤더 `09 24 01 00 01 09 00 01 01` |
| IF 4 | MIDI Streaming | 입력/출력 포트 각 1 (embedded + external jack 쌍, jack 총 4개). Bulk EP 0x02(OUT) / 0x83(IN), 512B |
| IF 5 | DFU | 런타임 DFU 1.1 |

### USBView 출력의 오해석

USBView는 오디오 클래스를 UAC1 기준으로 해석하므로 UAC2 부분의 다음 표시는 무시해야 한다. 값은 hex 덤프에서 직접 디코딩했다.

- "invalid bFunctionSubClass", "PC\_PROTOCOL\_UNDEFINED" 경고
- 채널 수 4, bBitResolution 7, 수십 MHz 샘플레이트
- IF 5 뒤의 HID 디스크립터 값 (실제로는 DFU 기능 디스크립터)

UAC2 AudioControl 디스크립터 길이 합계 119바이트(0x77)가 헤더 값과 일치하여 디코딩이 맞음을 뒷받침한다.

### MIDI 디스크립터 검증 결과

- MIDI 헤더 wTotalLength 65가 실제 디스크립터 합계와 일치
- jack ID 연결이 일관되고, 각 엔드포인트가 올바른 embedded jack을 가리킴
- **IF 3 AC 헤더의 `baInterfaceNr[1]` = 0x01** — MIDI 스트리밍 인터페이스(0x04)를 가리켜야 하므로 결함

### 덤프 대조 결과 (`docs/USB descriptor.txt`, 2026-10-25)

USBView 덤프를 hex 기준으로 직접 디코딩해 위 내용을 대조했다.

- **길이 일치**: Configuration `wTotalLength` 0x1A9(425)가 구성 요소 합과 일치한다(구성 9 + IAD 8 + IF 0 128 + IF 1 116 + IF 2 55 + IF 3 18 + IF 4 74 + IF 5 18). UAC2 AudioControl 119바이트, MIDI 65바이트도 각각 맞다.
- **결함 확인**: IF 3 헤더는 `09 24 01 00 01 09 00 01 01`이다. `bInCollection`=1, `baInterfaceNr[1]`=1로, 가리키는 IF 1은 UAC2 재생 스트림(서브클래스 0x02, 프로토콜 0x20)이다. 규격상 MIDI 스트리밍 인터페이스(IF 4, 서브클래스 0x03)여야 한다. 덤프에서 이 외의 구조적 이상은 찾지 못했다.
- **USBView 오해석**: UAC2 Format Type의 `bNrChannels` 4/2와 `bSubframeSize` 0x18/0x10은 각각 서브슬롯 크기(4B/2B)와 비트 해상도(24/16bit)를 잘못 읽은 값이다. DFU 영역의 "HID 디스크립터"는 DFU 기능 디스크립터(`bmAttributes` 0x07, 분리 타임아웃 250ms, 전송 크기 64, DFU 1.1)이고, 그 뒤의 긴 값 목록은 USBView가 디스크립터 끝을 넘어 읽은 쓰레기다.
- **인과는 여전히 미확인**: 결함이 사실이라는 것과 usbaudio.sys가 그 때문에 `C0000182`로 시작을 거부한다는 것은 별개다. 덤프로는 usbaudio.sys 내부 검증을 알 수 없다.
- **IF 3–4 묶음은 이 덤프로 해소되지 않음**: IAD(IF 0–2) 밖의 IF 3·4가 `MI_03` 하나로 묶인 이유(와 `MI_04`가 없는 이유)는 설명되지 않는다. 덤프의 "Open Pipes: 2"(IF 4의 벌크 EP)는 usbccgp가 IF 4를 포함해 구성을 선택했다는 추정 근거일 뿐, `MI_03`이 IF 4를 갖는다는 증명은 아니다.
- **자식 function에 전달되는 디스크립터 형태 미확인**: usbccgp가 `MI_03`에 IF 3–4만 필터링해 주는지, 설정 전체를 주는지 알 수 없다. 필터 드라이버의 1단계 로깅으로 확인한다.

## 장치 관리자 관찰 결과

상위 USB Composite Device 아래 자식 장치는 3개이며, `MI_04`는 별도 장치로 나타나지 않는다. MIDI function인 `MI_03`이 코드 10으로 실패한다.

| 자식 장치 | 인스턴스 경로 | 상태 | 해석 |
| --- | --- | --- | --- |
| SPARTAN CUE USB Audio 2.0 | `USB\VID_2360&PID_7011&MI_00\7&1cfe0ef2&0&0000` | 정상 | UAC2 오디오 (IF 0–2) |
| SPARTAN CUE USB Audio 2.0 | `USB\VID_2360&PID_7011&MI_03\7&1cfe0ef2&0&0003` | 오류: 코드 10, 문제 상태 `C0000182` | MIDI (IF 3–4로 추정) |
| AUDIOPROBE DFU | `USB\VID_2360&PID_7011&MI_05\7&1cfe0ef2&0&0005` | 경고 | 기본 드라이버 없음. MIDI와 무관 |

`C0000182`는 `STATUS_DEVICE_CONFIGURATION_ERROR`로, 드라이버가 장치 구성이 잘못되었다고 판단해 시작을 거부했다는 뜻이다. 즉 드라이버는 매칭·로드되었으나 디스크립터 검증 단계에서 실패했다.

## 원인 분석

관찰 결과가 추가될 때마다 가설을 좁혔다. 최종적으로 남은 유력 원인은 IF 3의 `baInterfaceNr` 오류다.

| 단계 | 근거 | 가설 | 결과 |
| --- | --- | --- | --- |
| 1 | IF 3–4가 IAD 밖에 있고, IAD가 있으면 usbccgp는 오디오용 옛 묶음 방식을 쓰지 않음 | IF 3과 IF 4가 별도 function으로 갈라짐 → IAD 추가 필요 | 기각: `MI_04`가 따로 없으므로 IF 3–4는 함께 묶인 것으로 보임 |
| 2 | 자식 장치 `MI_03`만 존재 | 드라이버 매칭 실패(코드 28) 가능성 | 기각: 코드 10, 드라이버는 로드됨 |
| 3 | 코드 10 + `C0000182` | 디스크립터 검증 실패. `baInterfaceNr`=1이 이 function에 없는 인터페이스를 가리켜 MIDI 스트리밍 인터페이스를 찾지 못함 | **유력, 미확정** |

### 유력 원인의 메커니즘

1. usbaudio.sys는 AudioControl 헤더의 `baInterfaceNr` 목록으로 자신의 스트리밍 인터페이스를 찾는다.
2. IF 3 헤더는 인터페이스 1을 가리키지만, 인터페이스 1은 UAC2 function(`MI_00`)에 속한다.
3. MIDI function 안에서 지정된 스트리밍 인터페이스를 찾지 못해 구성 오류로 시작을 거부한다.

이 메커니즘은 증상과 일치하지만, usbaudio.sys 내부 동작을 문서로 확인한 것은 아니다. `C0000182`는 다른 디스크립터 결함으로도 발생할 수 있다.

### 참고: 이전에 검토한 대안의 재평가

- **IAD 추가**: 묶음은 이미 이루어진 것으로 보여 우선순위에서 제외. 건드리면 열거 결과가 달라질 위험이 있음
- **usbccgp 열거 콜백 필터 드라이버**: 인터페이스 묶음만 바꿀 수 있고 `baInterfaceNr` 값은 고칠 수 없으므로 이번 원인에는 맞지 않음

## 권장 조치와 검증 절차

펌웨어에서 IF 3 AC 헤더의 마지막 바이트 1개를 수정하는 것이 첫 시도다. 길이가 바뀌지 않으므로 Configuration의 wTotalLength는 그대로 둔다.

```
현재:  09 24 01 00 01 09 00 01 01   (baInterfaceNr = 1)
수정:  09 24 01 00 01 09 00 01 04   (baInterfaceNr = 4)
```

값은 USBView 출력에서 역산했으므로, 실제 바이트는 펌웨어 소스에서 확인한다.

1. 펌웨어 디스크립터를 위와 같이 수정하고 장치에 올린다.
2. 이전 실패 상태가 캐시에 남지 않도록 `bcdDevice`를 올리거나, 장치 관리자에서 상위 USB Composite Device를 제거(드라이버 삭제는 체크하지 않음)한 뒤 재연결한다. 이 장치는 시리얼 번호가 없어 포트 기준으로 인스턴스가 기억된다.
3. `MI_03`의 코드 10이 사라졌는지 확인한다.
4. MIDI 앱(예: MIDI-OX)에서 입력/출력 포트가 보이는지 확인한다.
5. MIDI 루프백 송수신, SysEx 대용량 전송, 장시간 연속 전송, 케이블 분리·재연결을 시험한다.

수정 후에도 `C0000182`가 남으면 IF 3 AC 헤더의 다른 필드나 MIDI 디스크립터에 다른 결함이 있다는 뜻이다. 이 경우 Config 1 전체 hex나 USBPcap 캡처(GET\_DESCRIPTOR 응답)를 정상 MIDI 장치와 바이트 단위로 대조한다.

## 미해결 사항과 다음 단계

- [x] 펌웨어 디스크립터 수정 가능 여부 확인 → 불가. `MI_03` 하위 필터로 보정 (`docs/design/midi-descriptor-filter.md`). WinUSB + 사용자 모드 MIDI는 필터가 실패할 때의 대안
- [ ] `baInterfaceNr` 수정 후 `MI_03` 상태와 MIDI 포트 표시 확인
- [ ] `MI_03`이 실제로 IF 3–4를 모두 포함하는지 확인 (장치 관리자의 하드웨어/호환 ID, 1단계 로깅 필터의 설정 디스크립터 덤프)
- [ ] usbccgp가 `MI_03`에 주는 설정 디스크립터가 IF 3–4만 필터링한 것인지 전체(IF 0–5)인지 확인 (1단계 로깅 필터)
- [ ] `baInterfaceNr` 보정 후에도 `C0000182`가 남으면 정상 MIDI 장치와 바이트 단위로 대조
- [ ] Config 2 디스크립터 덤프 확보 (`bNumConfigurations` = 2)
- [ ] 오디오 샘플레이트 목록 확인 (USBPcap으로 Clock Source GET RANGE 응답 캡처)

## 참고 자료

- [USB Audio 2.0 drivers — Microsoft Learn](https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/usb-2-0-audio-drivers): UAC2 장치의 MIDI function은 별도 function으로 열거되어 usbaudio.sys가 로드됨
- [Support for interface collections — Microsoft Learn](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/support-for-interface-collections): IAD가 있으면 usbccgp는 IAD로 인터페이스를 묶고 오디오용 옛 방식을 쓰지 않음
