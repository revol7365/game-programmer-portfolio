# 조승원 게임 프로그래머 포트폴리오

C++ 서버 프로그래밍을 중심으로 동시성 자료구조, Windows IOCP 네트워크, 세션 인증, 데이터베이스 작업 큐를 학습하고 실제 코드와 재현 가능한 테스트로 정리한 저장소입니다. 학습 당시 코드의 구조와 흔적은 보존하고, 2026년 포트폴리오 정리 과정에서 수정한 내용은 각 README의 **현재 개선** 항목과 [변경 기록](docs/CHANGES.md)에 구분했습니다.

GitHub 프로필: [revol7365](https://github.com/revol7365)

## 프로젝트

| 프로젝트 | 확인된 목적 | 핵심 기술 | 2026-09-07 검증 |
|---|---|---|---|
| [Memory Pool](projects/memory-pool/README.md) | 다중 스레드 객체 재사용 풀 | C++20, Windows SLIST, atomic counters | Debug/Release PASS |
| [Lock-Free Queue](projects/lock-free-queue/README.md) | MPMC 큐와 안전한 노드 회수 | Michael-Scott queue, hazard pointer, atomics | Debug/Release PASS |
| [게임 서버 1: Multi-thread Chat](projects/multithread-chat-server/README.md) | IOCP 채팅과 섹터 broadcast | IOCP, 암호화 패킷, memory pool | 16접속·256응답 PASS |
| [게임 서버 2: Login-Chat System](projects/login-chat-system/README.md) | MySQL 로그인과 Redis 일회용 인증 | IOCP, MySQL, Redis Lua, DB work queue | 통합 흐름·DB 부하 PASS |
| [게임 서버 3: Group Echo](projects/group-echo-server/README.md) | group별 content thread와 mailbox | IOCP, actor-style group, MPSC mailbox | 16접속·160 echo PASS |

## 개발 환경과 기술

- Windows 11 Pro 10.0.26200
- Visual Studio Community 2022 17.14.12, MSVC 14.44.35207
- Windows SDK 10.0.26100.0
- C++17/C++20, WinSock2, IOCP, SRWLOCK, SLIST, C++ atomics
- MySQL Community Server/Client 8.0.44
- Redis for Windows 3.0.504(로컬 검증 환경)
- cpp_redis 4.3.1, tacopie 3.2.0, OpenSSL 3.0.17

외부 라이브러리의 출처, 고정 버전, 빌드 방식과 SHA-256은 [third_party 문서](third_party/README.md)에 있습니다.

## 전체 빌드

Git LFS가 설치된 Visual Studio 2022 Developer PowerShell에서 실행합니다.

```powershell
git lfs pull
.\scripts\Build-All.ps1
```

`GameServerPortfolio.sln`은 7개 실행 프로젝트를 Debug/Release x64로 빌드합니다. 외부 정적 라이브러리는 Release `/MD`로 빌드되어 서버 Debug 구성도 `/MD`, `_ITERATOR_DEBUG_LEVEL=0`, 최적화 해제와 PDB 생성을 사용합니다.

## 실행 및 테스트

```powershell
# Memory Pool, Lock-Free Queue: Debug와 Release 실행
.\scripts\Test-Core.ps1

# 독립 채팅 서버와 그룹 에코 서버: 서버를 띄우고 protocol probe 실행
.\scripts\Test-StandaloneServers.ps1
```

로그인·채팅 시스템은 MySQL과 Redis가 필요합니다. [.env.example](.env.example)의 이름으로 로컬 환경 변수를 설정하고 [tests/fixture.sql](tests/fixture.sql)을 적용한 뒤 LoginServer와 AuthenticatedChatServer를 실행합니다. 상세 절차는 [Login-Chat 테스트 결과](docs/test-results/login-chat-system.md)에 있습니다. DB 작업 큐는 [tests/dbserver_fixture.sql](tests/dbserver_fixture.sql)을 사용합니다.

## 검증 범위

완료한 항목은 전체 Debug/Release x64 빌드, 자료구조 기능·동시성 테스트, 독립 서버 protocol probe, 로그인→Redis 인증→채팅 흐름, replay 회귀 테스트, DBServer의 transaction/비transaction 작업 큐입니다. 모든 성공 판정은 종료 코드, 개수, 패킷 필드, checksum, Redis 최종 key 수처럼 코드가 확인할 수 있는 조건을 사용했습니다.

실제 인터넷 환경, 장시간 soak, WAN 지연, 16,384 동시 접속, 7일 연속 운전은 이번 정리에서 재현하지 않았습니다. 과거의 `15,000명·7일` 기록과 별도의 `16,384명 목표·400~500ms 관측` 기록은 동일 서버라는 근거가 없어 합산하거나 현재 성과로 주장하지 않습니다. [검증 개요](docs/test-results/README.md)와 프로젝트별 결과에서 재현 범위를 확인할 수 있습니다.

## 문서와 포트폴리오 파일

- [소스 프로젝트 대응표](docs/portfolio/SOURCE-MAPPING.md)
- [최초 실패와 현재 개선 기록](docs/CHANGES.md)
- [프로젝트별 테스트 결과](docs/test-results/README.md)
- [외부 의존성](third_party/README.md)

PC에서 2025년 3월 버전 포트폴리오 PDF를 찾았지만 이메일, 전화번호, 생년월일이 포함되어 공개 저장소에서 제외했습니다. 개인정보를 제거한 공개용 PDF는 현재 저장소에 없습니다.
