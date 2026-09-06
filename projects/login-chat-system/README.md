# Login-Chat Integration System

## 프로젝트 목적

LoginServer, Authenticated ChatServer, DBServer를 연결해 MySQL 계정 조회, Redis 일회용 session key 등록·소비, 인증 후 채팅 접속, 게임 저장 작업 큐를 검증하는 시스템입니다.

## 사용 기술

C++17/C++20, Windows IOCP, MySQL 8.0.44, Redis, cpp_redis 4.3.1, Lua compare-and-delete, atomics, producer queue와 single DB writer.

## 전체 구조

- `LoginServer`: MySQL 계정 조회 후 client가 보낸 64-byte session key를 Redis에 60초 TTL로 저장
- `ChatServer`: Redis Lua compare-and-delete 성공 후 player를 인증하고 sector/chat 처리
- `DBServer`: level/money/quest/item/trade 메시지를 queue로 받아 transaction 사용 여부를 비교
- `RuntimeConfig.h`와 `NetLib/Define.h`: 환경 변수 기반 로컬 설정

## 내가 구현한 핵심 부분

content thread별 player map과 DB connection, 로그인 응답 packet, Redis session key 저장, 채팅 서버의 원자적 key 소비와 중복 로그인 교체 순서를 구현했습니다. DBServer는 producer가 만든 query message를 single writer가 처리합니다.

## 개발 중 마주친 문제

DB 비밀번호와 주소가 코드에 남아 있었고, 인증되지 않은 replay 연결이 Redis 검사 전에 기존 정상 세션을 끊었습니다. DBServer는 worker 시작 뒤 transaction mode를 바꾸는 data race와 64-bit SQL 서식 불일치가 있었습니다. 외부 라이브러리의 출처와 경로도 통일되지 않았습니다.

## 세운 가설과 확인 방법

Redis key 검증과 기존 세션 교체의 순서가 replay eviction의 원인이라고 보고 compare-and-delete 성공 뒤에만 교체하도록 변경했습니다. invalid token과 이미 소비한 token을 각각 다시 전송한 뒤 기존 정상 연결에서 sector 응답이 계속 오는지 확인했습니다. DBServer는 두 mode에서 각각 6,000건을 실행해 query failure를 집계했습니다.

## 해결 또는 현재 상태

접속 정보는 `PORTFOLIO_DB_*`, `PORTFOLIO_REDIS_*` 환경 변수로 분리했습니다. 16개 계정의 로그인→Redis 인증→채팅, invalid/replay 거절, 256 chat response, 기존 session 유지, Redis `DBSIZE 0`을 검증했습니다. DBServer 두 mode도 각각 6,000건, query failure 0입니다.

## 빌드 및 실행 방법

```powershell
msbuild .\GameServerPortfolio.sln /p:Configuration=Release /p:Platform=x64
# .env.example의 변수 이름을 현재 PowerShell 환경에 설정
# tests/fixture.sql을 MySQL에 적용
.\projects\login-chat-system\LoginServer\x64\Release\003_Login_Server.exe
.\projects\login-chat-system\ChatServer\x64\Release\002_Multi_Thread_Chating_Server.exe
```

DBServer는 `tests/dbserver_fixture.sql`을 적용한 뒤 `projects/login-chat-system/DBServer/x64/Release/DBServer.exe`를 실행합니다.

## 테스트 환경과 결과

Windows 11, Visual Studio 2022 17.14.12, MySQL 8.0.44, Redis for Windows 3.0.504, Release x64입니다. 포트와 명령 순서는 [테스트 결과](../../docs/test-results/login-chat-system.md)에 있습니다.

## 남아 있는 한계와 개선 계획

session key는 LoginServer가 새로 생성하지 않고 client가 보낸 값을 등록합니다. TLS, password hashing, token signing, Redis HA는 구현 범위가 아닙니다. 테스트용 Redis 버전은 오래된 Windows port이므로 운영 환경에서는 지원되는 Redis 배포판으로 교체해야 합니다.
