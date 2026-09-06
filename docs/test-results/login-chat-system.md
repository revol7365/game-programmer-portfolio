# Login-Chat System test

- 환경: Windows 11 Pro, Visual Studio 2022 17.14.12, MySQL 8.0.44, Redis for Windows 3.0.504
- 구성/플랫폼: LoginServer, AuthenticatedChatServer, DBServer Debug/Release x64 빌드; Release x64 실행
- 날짜: 2026-09-07
- 빌드: 세 프로젝트 모두 PASS / PASS
- 설정: `.env.example` 변수 이름 사용, 실제 값은 저장하지 않음
- fixture: `tests/fixture.sql`, `tests/dbserver_fixture.sql`
- 격리 포트: MySQL 33062, Redis 6380, Login 6001, Chat 6000

## 로그인과 채팅

1. MySQL/Redis를 loopback 격리 포트로 실행합니다.
2. `tests/fixture.sql`을 적용합니다.
3. 환경 변수를 설정하고 LoginServer와 AuthenticatedChatServer를 실행합니다.
4. `.\scripts\Test-LoginChat.ps1 -RedisCliPath <redis-cli.exe 경로>`를 실행합니다. 이 스크립트는 두 서버를 실행하고 `ProtocolProbe.FullIntegration`을 호출한 뒤 종료합니다.

결과는 login 16, Redis-token chat login 16, invalid token 거절 1, replay 거절 1, sector move 16, validated chat responses 256입니다. replay 시도 후 기존 정상 session은 유지됐고 종료 뒤 Redis `DBSIZE`는 0입니다.

## DB 작업 큐

`tests/dbserver_fixture.sql` 적용 후 DBServer를 실행했습니다. transaction과 non-transaction mode 각각 6,000 messages, query failure 0으로 PASS했습니다.

로그: [login-chat-system-20260907.txt](logs/login-chat-system-20260907.txt)

TLS, token signing, Redis HA, 운영 보안 설정과 장시간 부하는 검증하지 않았습니다.
