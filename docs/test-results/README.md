# Test results

검증일은 2026-09-07입니다.

- Windows 11 Pro 10.0.26200
- Visual Studio Community 2022 17.14.12
- MSVC 14.44.35207, Windows SDK 10.0.26100.0
- Debug/Release, x64
- MySQL 8.0.44, Redis for Windows 3.0.504

| 프로젝트 | 빌드 | 실행 검증 |
|---|---|---|
| [Memory Pool](memory-pool.md) | Debug/Release PASS | 기능·16 thread·수명 PASS |
| [Lock-Free Queue](lock-free-queue.md) | Debug/Release PASS | FIFO, 8P/8C × 10 rounds PASS |
| [Multi-thread Chat](multithread-chat-server.md) | Debug/Release PASS | 16 clients, 256 responses PASS |
| [Login-Chat System](login-chat-system.md) | 3개 구성요소 Debug/Release PASS | 인증 흐름·replay·DB queue PASS |
| [Group Echo](group-echo-server.md) | Debug/Release PASS | 16 clients, 160 echoes PASS |

전체 빌드는 `.\scripts\Build-All.ps1`로 수행했습니다. 일부 기존 한글 주석 파일은 혼합 인코딩 때문에 MSVC `C4819` 경고가 남지만 빌드 오류는 0건입니다. 자료형 서식/축소 변환과 signed 비교에서 확인된 `C4477`, `C4244`, `C4018`은 수정 후 재빌드했습니다.

이번 테스트에서 WAN, 장시간 soak, 15,000명, 16,384명, 7일 연속 운전은 재현하지 않았습니다. 과거 기록은 서로 합치지 않으며 성과 수치로 사용하지 않습니다.
