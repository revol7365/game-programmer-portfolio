# Multi-thread Chat Server test

- 환경: Windows 11 Pro, Visual Studio 2022 17.14.12
- 구성/플랫폼: Debug x64, Release x64 빌드; Release x64 실행
- 날짜: 2026-09-07
- 빌드: PASS / PASS
- 실행: `.\scripts\Test-StandaloneServers.ps1`
- 절차: server를 loopback `127.0.0.1:6000`에 실행하고 C# protocol probe 16개 연결
- 판정: packet code, payload length, checksum, 암복호화, type, accountNo, message body 비교
- 결과: login 16, sector move 16, validated chat responses 256, PASS
- 로그: [multithread-chat-server-20260907.txt](logs/multithread-chat-server-20260907.txt)

Dummy Client GUI의 대응 프로젝트는 확인되지 않아 실행하지 않았습니다. WAN, soak, 대규모 접속 성능은 미검증입니다.
