# 프로젝트 지침

모든 작업 단계에 공통으로 적용되는 규칙입니다. 단계별 절차는 스킬(`.claude/skills/`)에 있습니다.

## 프로젝트 개요
- 목표: USB Audio 장치 "CUE"의 필터 드라이버 개발
- 대상 장치: `USB\VID_2360&PID_7011` (복합 장치면 `&MI_xx` 인터페이스 노드)
- 프레임워크: KMDF, 언어: C++ (커널 모드 제약 준수)
- 타깃: Windows 11 x64 단일
- 도구: Visual Studio 2026 + WDK
- 필터 위치(상위/하위, 장치 노드/인터페이스 노드)는 미정 → 첫 `/design-doc`에서 결정한다
- 테스트 시스템(VM/별도 PC)은 준비되면 이 문서에 기록한다: (미정)

## 명령어
Developer 환경(`vcvars64.bat`)이 잡힌 셸에서 실행한다.
- 빌드: `msbuild DrvCUE.sln /p:Configuration=Debug /p:Platform=x64`
- 릴리스 빌드: `msbuild DrvCUE.sln /p:Configuration=Release /p:Platform=x64`
- 코드 분석: 빌드에 `/p:RunCodeAnalysis=true` 추가
- INF 검증: `infverif /w <INF 경로>`
- 정적 드라이버 검증(SDV): VS의 Static Driver Verifier에서 KMDF 규칙으로 실행
- 단위/통합 테스트: 테스트 시스템 준비 후 기록 예정

## 코딩 규칙
- 커널 모드 C++ 제약: 예외·RTTI·STL·CRT 금지, 전역 생성자/소멸자 의존 금지. `new`/`delete`는 풀 태그를 쓰는 오버로드로만 사용
- 진입점과 WDF 콜백은 `extern "C"`로 선언
- 모든 `NTSTATUS` 반환값을 검사하고 실패 경로에서 자원을 정리한다
- SAL 주석과 `_IRQL_requires_` 계열 주석을 붙인다
- 메모리는 `ExAllocatePool2`와 고유 풀 태그를 사용한다
- 사용자 모드에서 온 버퍼(IOCTL 등)는 길이·정렬을 검증한 뒤 사용한다
- 부동소수점 금지, 커널 스택 사용 최소화, 문자열은 `Rtl*Safe`/`ntstrsafe` 사용
- 함수는 한 가지 일만 하도록 작게 유지
- 주변 코드의 네이밍·주석 스타일을 따른다
- 새 의존성(라이브러리, 패키지) 추가 전에 반드시 사용자에게 확인

## 안전 규칙
- 이 PC에서 드라이버를 설치·로드하지 않는다. 설치와 테스트는 테스트 시스템에서만 한다
- `pnputil`, `bcdedit`, `sc`, `verifier`, `devcon`, `signtool sign` 실행 전에는 반드시 사용자에게 확인한다
- 서명 인증서와 개인 키(`*.pfx`, `*.pvk`, `*.snk`)는 읽거나 커밋하지 않는다
- WDK 도구가 한글 경로에서 실패할 수 있으니 인증서·출력물은 영문 경로에 둔다

## 작업 흐름
작업 규모에 따라 단계를 고른다. 커널 코드는 한 줄 수정도 크래시를 낼 수 있으므로 빌드·분석 통과까지는 항상 한다.

| 규모 | 흐름 |
|---|---|
| 작은 수정 (파일 1~2개) | 바로 수정 → 빌드 + 코드 분석 (+ 테스트 시스템 로드 확인) |
| 중간 기능 | Plan 모드로 계획 → 승인 후 개발 → `reviewer` 에이전트로 리뷰 |
| 큰 기능 | `/design-doc`로 설계 문서 확정 → 개발 → `/test-plan` → `reviewer` 리뷰 |

## 완료 기준
작업을 "완료"라고 보고하기 전에 반드시:
1. 경고 0으로 빌드되고 코드 분석, `infverif`가 통과한다
2. 새 동작에는 테스트(또는 테스트 시스템에서의 확인 절차)가 있다
3. 중간 이상 규모면 `reviewer` 에이전트의 리뷰를 받고 지적 사항을 처리했다
4. 테스트 시스템에서 확인하지 못한 항목(하드웨어 없음, VM 없음 등)은 숨기지 않고 명시한다. 빌드만 통과한 상태를 "동작 확인"으로 보고하지 않는다

## 커밋
- Conventional Commits 형식: `feat:`, `fix:`, `refactor:`, `test:`, `docs:`
- 사용자가 요청할 때만 커밋한다
