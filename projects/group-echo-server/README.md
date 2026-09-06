# Group Echo Server

## 프로젝트 목적

네트워크 worker와 content logic을 분리하고, player가 login group과 echo group 사이를 이동하는 actor-style group 구조를 검증하는 서버입니다.

## 사용 기술

C++20, Windows IOCP, group별 content thread, `CRITICAL_SECTION` 기반 MPSC mailbox, object pool, packet checksum/암복호화.

## 전체 구조

- `Server/NetLib`: IOCP session과 packet 계층
- `Server/GroupServer/Group.h`: JOIN/LEAVE/PACKET/GROUP_ENTER mailbox와 group update
- `LoginGroup`: 로그인 후 다음 group으로 player 소유권 전달
- `EchoGroup`: account/tick echo 처리
- `GroupManager`: group 생성과 조회

## 내가 구현한 핵심 부분

worker가 job을 mailbox에 넣고 group의 단일 content thread가 drain/dispatch하도록 구성했습니다. group 이동 중 LEAVE가 이전 group에 늦게 도착하면 현재 group으로 전달해 premature free를 피하는 경로를 포함합니다.

## 개발 중 마주친 문제

프로토콜 설명은 72-byte login payload였지만 구현은 근거 없는 4 bytes를 추가로 요구했습니다. 기존 lock-free job queue는 ABA/debug break/spin 위험이 있어 group 소유권 검증에 방해가 됐습니다.

## 세운 가설과 확인 방법

문서와 구현의 payload 차이가 client 상호운용 실패 원인이라고 보고 72 bytes로 통일했습니다. account와 tick을 포함한 echo packet을 16개 연결에서 10회씩 보내고 응답의 type, 길이, account, tick, checksum을 비교했습니다.

## 해결 또는 현재 상태

login payload를 72 bytes로 통일하고 group mailbox를 명확한 MPSC/single-consumer 구조로 정리했습니다. Debug/Release x64 빌드와 Release 160 echo response 검증이 통과합니다.

## 빌드 및 실행 방법

```powershell
msbuild .\GameServerPortfolio.sln /t:GroupEchoServer /p:Configuration=Release /p:Platform=x64
.\scripts\Test-StandaloneServers.ps1
```

기본 bind는 `127.0.0.1:16003`입니다.

## 테스트 환경과 결과

Windows 11, Visual Studio 2022 17.14.12, Release x64에서 16개 client × 10 echo를 실행했습니다. [테스트 결과](../../docs/test-results/group-echo-server.md)를 참고하세요.

## 남아 있는 한계와 개선 계획

mailbox는 lock-free가 아닌 `CRITICAL_SECTION` 기반입니다. 현재 검증은 기능과 소유권 전달에 집중했으며 group 수 증가, mailbox backlog, starvation과 장시간 부하는 측정하지 않았습니다.
