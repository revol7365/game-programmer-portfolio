# Windows x64 검증 결과

검증 환경: Windows, Visual Studio Community 2022 17.14.12, MSVC v143, Windows SDK 10.0.26100.0, MySQL 8.0.44 test instance, Redis for Windows 3.0.504.

## 빌드

2026-09-07에 `GameServerPortfolio.sln`을 `/m /t:Rebuild`로 실행했습니다.

| 프로젝트 | Debug x64 | Release x64 |
|---|---:|---:|
| MemoryPool | PASS | PASS |
| LockFreeQueue | PASS | PASS |
| MultiThreadChatServer | PASS | PASS |
| AuthenticatedChatServer | PASS | PASS |
| LoginServer | PASS | PASS |
| DBServer | PASS | PASS |
| GroupEchoServer | PASS | PASS |

서버 Debug는 Release로 배포된 cpp_redis/tacopie/MySQL client와 ABI를 맞추기 위해 `/MD`, `_ITERATOR_DEBUG_LEVEL=0`을 사용합니다. 최적화는 꺼지고 PDB는 생성됩니다.

전체 rebuild에는 원본 파일의 혼합된 한글 주석 인코딩에서 발생하는 `C4819` 경고가 일부 남습니다. 문자셋 일괄 변환으로 주석을 손상시키지 않기 위해 원본 인코딩을 유지했습니다. 자료형 불일치와 관련된 `C4477`, `C4244`, `C4018` 진단은 수정 후 재빌드에서 0건입니다.

## 실행 검증

| 대상 | 구성 | 결과 |
|---|---|---|
| MemoryPool | Debug + Release | 기능 PASS, 16 threads × 100,000 alloc/free PASS, pool 소멸 후 live object 0 |
| LockFreeQueue | Debug + Release | 각 구성 8 producers + 8 consumers, 1,600,000 items × 10 rounds; missing 0, duplicate 0, corrupt 0 |
| Standalone Chat | Release | simultaneous clients 16, logins 16, sector moves 16, validated responses 256 |
| Login + Chat | Release | DB logins 16, Redis-auth chat logins 16, invalid/replay rejection 각 1, sector moves 16, validated chat responses 256 |
| Replay regression | Release | 재사용 키 연결 거절 후 기존 정상 세션의 추가 sector move 응답 확인 |
| Redis postcondition | Release | flow 종료 시 DBSIZE 0 |
| DB work queue | Release | transaction/비transaction 각 6,000 messages, query failures 0 |
| Group Echo | Release | simultaneous clients 16, logins 16, validated echo responses 160 |

서버 probe는 packet code, payload length, checksum, 암복호화, packet type, accountNo와 message body를 대조합니다. 테스트 실행 로그는 로컬 `tests/results`에 생성되며 Git에서는 제외됩니다.

## 재현 명령

```powershell
.\scripts\Build-All.ps1
.\scripts\Test-Core.ps1
.\scripts\Test-StandaloneServers.ps1
```

Login–Chat 통합 테스트에는 격리된 MySQL과 Redis가 필요합니다. `tests/fixture.sql`을 적용하고 환경 변수를 설정한 뒤 `.\scripts\Test-LoginChat.ps1 -RedisCliPath <redis-cli.exe 경로>`를 실행합니다.
