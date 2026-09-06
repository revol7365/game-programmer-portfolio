# Lock-Free Queue

## 프로젝트 목적

여러 producer와 consumer가 동시에 접근하는 MPMC queue를 구현하고, 노드 회수 과정의 use-after-free와 ABA 위험을 테스트로 검증하는 프로젝트입니다.

## 사용 기술

C++20, Michael-Scott queue, `std::atomic`, hazard pointer, thread-local hazard record, Visual Studio 2022.

## 전체 구조

- `LockFreeQueue.h`: sentinel 기반 MPMC queue와 hazard-pointer reclamation
- `main.cpp`: FIFO 검사와 8 producer/8 consumer stress
- `LockFreeQueue.vcxproj`: Debug/Release x64 프로젝트

## 내가 구현한 핵심 부분

head/tail CAS, 뒤처진 tail 보정, 두 개의 hazard pointer로 head와 next를 보호하는 dequeue 경로를 구현했습니다. 제거한 node는 retired list에 넣고 활성 hazard set에 없는 node만 회수합니다.

## 개발 중 마주친 문제

기존 코드는 잘못된 `InterlockedCompareExchange64` 캐스팅으로 컴파일되지 않았고, 임시 수정 뒤에는 size 불일치와 데이터 손실이 발생했습니다. stress 실패를 출력하면서 종료 코드 0을 반환해 자동 검증도 신뢰할 수 없었습니다.

## 세운 가설과 확인 방법

큐 알고리즘 자체보다 제거 직후 node 회수가 concurrent reader와 충돌한다고 가정했습니다. 각 값을 한 번만 소비했는지 확인하는 atomic seen 배열로 누락, 중복, 범위 손상을 10회 반복 측정했습니다.

## 해결 또는 현재 상태

Michael-Scott MPMC queue에 hazard-pointer 회수를 적용하고 실패 시 프로세스가 1을 반환하도록 수정했습니다. Debug/Release 각 1,600,000 items × 10 rounds에서 누락·중복·손상 0입니다.

## 빌드 및 실행 방법

```powershell
msbuild .\GameServerPortfolio.sln /t:LockFreeQueue /p:Configuration=Release /p:Platform=x64
.\projects\lock-free-queue\x64\Release\LockFreeQueue.exe
```

## 테스트 환경과 결과

Windows 11, Visual Studio 2022 17.14.12, x64에서 single-thread FIFO와 8P/8C stress를 실행했습니다. 상세 결과는 [테스트 결과](../../docs/test-results/lock-free-queue.md)에 있습니다.

## 남아 있는 한계와 개선 계획

hazard record는 queue 수명 동안 유지되며 thread 등록 해제 API가 없습니다. 장기 실행에서 thread가 반복 생성되는 환경을 위한 record 재사용과 메모리 상한 검증이 다음 과제입니다.
