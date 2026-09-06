# Group Echo Server test

- 환경: Windows 11 Pro, Visual Studio 2022 17.14.12
- 구성/플랫폼: Debug x64, Release x64 빌드; Release x64 실행
- 날짜: 2026-09-07
- 빌드: PASS / PASS
- 실행: `.\scripts\Test-StandaloneServers.ps1`
- 절차: loopback `127.0.0.1:16003`에 16 clients 접속, client당 account/tick echo 10회
- 판정: packet type, payload length, checksum, accountNo와 tick 비교
- 결과: login 16, validated echo responses 160, PASS
- 로그: [group-echo-server-20260907.txt](logs/group-echo-server-20260907.txt)

group 수 증가, mailbox backlog, starvation, WAN과 장시간 부하는 검증하지 않았습니다.
