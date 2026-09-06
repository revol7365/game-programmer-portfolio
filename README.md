# 조승원 | C++ 게임 서버 포트폴리오

C++로 동시성 자료구조와 Windows IOCP 서버를 구현했습니다. 수업에서 작성한 코드를 그대로 두지 않고, 포트폴리오를 준비하며 다시 빌드하고 실제 동작을 확인해 발견한 문제를 수정했습니다.

- GitHub: [revol7365](https://github.com/revol7365)
- 개발 환경: Windows 11, Visual Studio 2022, x64
- 포트폴리오 PDF: 최종 검토 후 `portfolio/`에 추가 예정

## 프로젝트

가장 먼저 볼 프로젝트는 **Login-Chat System**입니다. 로그인 요청부터 Redis 토큰 검증, 채팅 서버 접속, DB 작업 큐까지 서버 사이의 흐름을 한 프로젝트에서 확인할 수 있습니다.

| 프로젝트 | 핵심 내용 | 검증 결과 |
| --- | --- | --- |
| [Login-Chat System](projects/login-chat-system/README.md) | IOCP, MySQL, Redis Lua, DB 작업 큐 | 정상 인증과 잘못된 토큰·재사용 토큰 거절 확인 |
| [Multi-thread Chat Server](projects/multithread-chat-server/README.md) | IOCP 채팅, 섹터 이동, 패킷 암복호화 | 16명 동시 접속, 채팅 응답 256개 확인 |
| [Group Echo Server](projects/group-echo-server/README.md) | 그룹별 콘텐츠 스레드, MPSC mailbox | 16명 동시 접속, echo 응답 160개 확인 |
| [Lock-Free Queue](projects/lock-free-queue/README.md) | Michael–Scott MPMC queue, hazard pointer | 8 producer·8 consumer, 1,600,000개 × 10회 통과 |
| [Memory Pool](projects/memory-pool/README.md) | Windows x64 SLIST 기반 객체 풀 | 16스레드 반복 테스트, 소멸 후 객체 0개 확인 |

각 프로젝트 README에 구조, 처음 발생한 문제, 수정한 내용, 실행 방법을 적었습니다.

## 검증 요약

- 7개 Visual Studio 프로젝트 Debug/Release x64 빌드 통과
- Memory Pool 기능·다중 스레드·객체 수명 테스트 통과
- Lock-Free Queue 누락·중복·손상 0건
- 세 서버의 로그인·섹터 이동·채팅 및 echo 응답 확인
- Login-Chat System의 MySQL 로그인, Redis 인증, 토큰 재사용 거절 확인

과거 자료의 **15,000명·7일**과 **16,384명 목표·400~500ms 관측**은 같은 서버 기록인지 확인되지 않았고 이번 테스트에서 재현하지도 않았습니다. 따라서 현재 검증 결과와 합치지 않았습니다.

<details>
<summary><strong>빌드와 상세 검증 방법</strong></summary>

Git LFS와 Visual Studio 2022 C++ 워크로드가 필요합니다.

```powershell
git lfs pull
.\scripts\Build-All.ps1
.\scripts\Test-Core.ps1
.\scripts\Test-StandaloneServers.ps1
```

Login-Chat System 통합 테스트는 MySQL과 Redis를 실행하고 `tests/fixture.sql`을 적용한 뒤 진행합니다.

```powershell
.\scripts\Test-LoginChat.ps1 -RedisCliPath <redis-cli.exe 경로>
```

상세 기록:

- [처음 발견한 문제와 수정 내용](docs/CHANGES.md)
- [전체 검증 절차와 관측값](docs/VALIDATION.md)
- [프로젝트별 테스트 결과](docs/test-results/README.md)
- [원본 프로젝트와 현재 폴더 대응표](docs/portfolio/SOURCE-MAPPING.md)
- [외부 라이브러리의 출처와 버전](third_party/README.md)

</details>
