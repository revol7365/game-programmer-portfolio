# Multi-thread Chat Server

## 프로젝트 목적

Windows IOCP 기반 다중 접속 서버에서 암호화 패킷 처리, player/session 관리, sector 이동과 주변 사용자 채팅 broadcast를 구현한 독립 실행형 채팅 서버입니다.

## 사용 기술

C++20, WinSock2, IOCP, SRWLOCK, memory pool, lock-free queue, packet checksum/암복호화, Visual Studio 2022.

## 전체 구조

- `Server/NetLib`: IOCP accept/recv/send, session, packet, ring buffer, monitor
- `Server/ChatingServer`: player, sector table, login/move/chat/heartbeat 처리
- `Server/DBConnect`: 인증형 변형과 공유하는 Redis adapter
- `tests/ProtocolProbe.cs`: 실제 wire packet을 생성·검증하는 client probe

## 내가 구현한 핵심 부분

IOCP completion 처리와 session lifecycle, 패킷 framing/checksum/암복호화, sector 주변 테이블, player 이동과 broadcast 경로를 구현했습니다. 포트폴리오의 독립 실행판은 Redis 인증을 끄고 loopback에서 protocol 동작을 재현합니다.

## 개발 중 마주친 문제

Release로 제공된 Redis 정적 라이브러리와 Debug CRT/iterator ABI가 충돌했고, 외부 라이브러리 경로가 프로젝트마다 중복돼 다른 PC에서 재현하기 어려웠습니다.

## 세운 가설과 확인 방법

컴파일러 설정이 아니라 CRT/iterator ABI 불일치가 linker 오류의 원인이라고 보고 Debug를 `/MD`, `_ITERATOR_DEBUG_LEVEL=0`으로 맞춘 뒤 전체 Rebuild했습니다. 서버 성공 문구 대신 16개 실제 socket으로 응답 패킷의 type, account, body, checksum을 검사했습니다.

## 해결 또는 현재 상태

공식 cpp_redis 4.3.1/tacopie 3.2.0을 공용 `third_party`에서 참조하고 Debug/Release x64를 통과했습니다. Release probe에서 16 login, 16 sector move, 256 chat response를 검증했습니다.

## 과거 Content Thread 비교 실험

Error Tracking 기록에서 로그를 줄인 뒤에도 장시간 지연이 남아, 단일 Content Queue의 직렬 처리를 병목 후보로 두었습니다. 동일 장비와 동일한 더미 클라이언트 설정에서 Content Thread 수만 1개에서 4개로 바꿔 비교했습니다.

4개 구성은 `CONTENT_THREAD_CNT = 4`와 `SessionID % 4`로 담당 Thread를 고정하고, Join·Leave·Recv Job을 네 IOCP Queue에 분산했습니다. 같은 세션의 작업 순서는 한 Thread 안에서 유지했습니다.

| 접속 수 | 구성 | Accept TPS | Recv TPS | Send TPS | 더미 평균 응답(ms) |
| ---: | --- | ---: | ---: | ---: | --- |
| 5,000 | Single | 2,677 | 23,068 | 111,075 | 58 |
| 5,000 | Multi | 1,399 | 57,838 | 191,765 | 6 |
| 10,000 | Single | 2,906 | 10,010 | 49,776 | 370 / 540 |
| 10,000 | Multi | 1,790 | 71,828 | 281,873 | 7 / 6 |
| 15,000 | Single | 2,282 | 5,239 | 24,267 | 285 / 1,193 / 1,188 |
| 15,000 | Multi | 4,451 | 122,064 | 986,477 | 10 / 7 / 3 |

동일 조건의 Multi 구성에서 TPS가 높아지고 평균 응답 지연이 낮아졌습니다. 단일 Content Queue의 직렬 처리가 병목에 영향을 주고 있음을 확인했습니다. 대신 `SessionID % 4` 방식은 Thread별 부하가 고르게 분산된다는 보장이 없다는 비용이 남습니다.

수치는 학습 당시 직접 작성한 [원본 테스트 기록](https://github.com/user-attachments/files/31890366/default.xlsx)입니다. 동일 장비에서 비교했지만 세부 하드웨어 사양, 측정 시간과 반복 횟수는 기록이 남아 있지 않아 현재 환경의 성능 보증값이나 고정된 개선 배수로 사용하지 않습니다.

## 빌드 및 실행 방법

```powershell
msbuild .\GameServerPortfolio.sln /t:MultiThreadChatServer /p:Configuration=Release /p:Platform=x64
.\scripts\Test-StandaloneServers.ps1
```

기본 bind는 `127.0.0.1:6000`입니다.

## 테스트 환경과 결과

Windows 11, Visual Studio 2022 17.14.12, Release x64에서 16개 동시 client를 사용했습니다. [테스트 결과](../../docs/test-results/multithread-chat-server.md)에 절차와 관측값을 기록했습니다.

## 남아 있는 한계와 개선 계획

이번 검증은 loopback 16접속 기능 검사이며 WAN, 장시간 soak, 대규모 동시 접속 성능을 검증하지 않았습니다. Dummy Client GUI 프로젝트는 현재 포트폴리오 정리본에 포함되지 않아 C# protocol probe로 대체했습니다.
