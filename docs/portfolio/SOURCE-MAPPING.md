# Source project mapping

2026-09-07에 `C:\Users\revol\source\repos`의 코드, 기존 Git branch/commit 정보, Visual Studio project 구성, 실행 결과를 대조했습니다. 원본 저장소는 수정하지 않았습니다.

| 포트폴리오 경로 | 실제 프로젝트 | 확인 근거 | 원본 기준 |
|---|---|---|---|
| `projects/memory-pool` | MemoryPool | `MemoryPool.vcxproj`, template pool 구현, Function/MultiThread/Lifetime 테스트 | `Procademy-Project/Course 04/MemoryPool`, master 계열 |
| `projects/lock-free-queue` | LockFreeQueue | `LockFreeQueue.vcxproj`, Michael-Scott MPMC와 stress test | `Procademy-course03-Project/LockFreeQueue`, master 계열 |
| `projects/multithread-chat-server` | Multi-thread Chat Server | IOCP `BaseServer`, `ChatServer`, sector/chat packet과 probe | commit `6fdc6d9b682fa59a747321385533e35fb163c8d2` 계열 |
| `projects/login-chat-system` | LoginServer + ChatServer + DBServer | MySQL account query, Redis key, authenticated chat, DB work queue | commit `c1428e7` 계열 |
| `projects/group-echo-server` | Group Echo Server | `Group` mailbox, LoginGroup/EchoGroup, echo protocol | commit `7597a5e` 계열 |

로컬 원본에는 별도 미커밋 파일이 있었지만 포트폴리오 저장소 생성 과정에서 원본의 status나 branch를 변경하지 않았습니다. 기존 Dummy Client의 완전한 대응 프로젝트는 정리본에서 확인되지 않아 임의로 포함하지 않았고, 최소 실행 검증은 `tests/ProtocolProbe.cs`로 수행했습니다.

학습 당시 구현과 2026년 개선은 [변경 기록](../CHANGES.md)에서 구분합니다. 과거 `15,000명·7일` 기록과 `16,384명 목표·400~500ms 관측` 기록은 동일 서버라는 증거가 없어 서로 연결하지 않으며 현재 검증 결과에도 포함하지 않습니다.
