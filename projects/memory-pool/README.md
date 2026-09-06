# Memory Pool

## 프로젝트 목적

다중 스레드 서버에서 반복되는 객체 할당을 재사용 풀로 바꾸고, 풀의 수명 종료까지 객체가 안전하게 회수되는지 확인하는 프로젝트입니다.

## 사용 기술

C++20, Windows x64 `SLIST`, `InterlockedPushEntrySList`/`InterlockedPopEntrySList`, C++ atomics, Visual Studio 2022.

## 전체 구조

- `LockFreeMemoryPool.h`: SLIST free-list, 소유자/cookie 검사, capacity/in-use 계수
- `main.cpp`: 기능, 16스레드, 객체 수명, benchmark 테스트
- `MemoryPool.vcxproj`: Debug/Release x64 프로젝트

## 내가 구현한 핵심 부분

`T`를 포함한 정렬된 node를 풀에서 재사용하고, 반환 포인터에서 node를 역산해 다른 풀의 포인터를 거부합니다. Windows x64 SLIST가 제공하는 lock-free push/pop과 ABA 대응을 사용하며, worker 종료 후 checked-out 객체가 없는 경우 free-list를 정리합니다.

## 개발 중 마주친 문제

기존 TLS cache 구현에서는 worker thread 종료 뒤 객체가 중앙 풀로 완전히 돌아오지 않아 풀 소멸 시 live object가 남았습니다. 기존 benchmark는 메모리 사용량이 2 GiB 이상 증가하며 완료되지 않았습니다.

## 세운 가설과 확인 방법

문제 원인을 thread-local cache의 수명과 중앙 풀 반환 시점으로 보고, TLS를 제거한 SLIST 구현에서 16개 thread가 각각 100,000회 alloc/free한 뒤 `in-use == 0`과 전역 live count를 확인했습니다.

## 해결 또는 현재 상태

SLIST free-list로 교체해 thread 종료와 무관하게 반환 경로를 하나로 만들었습니다. Debug/Release에서 기능·동시성·수명 검사가 모두 통과합니다.

## 빌드 및 실행 방법

```powershell
msbuild .\GameServerPortfolio.sln /t:MemoryPool /p:Configuration=Release /p:Platform=x64
.\projects\memory-pool\x64\Release\MemoryPool.exe
```

## 테스트 환경과 결과

Windows 11, Visual Studio 2022 17.14.12, x64에서 Debug/Release 모두 PASS입니다. 16 threads × 100,000 alloc/free, 종료 후 live object 0을 확인했습니다. 상세 수치는 [테스트 결과](../../docs/test-results/memory-pool.md)에 있습니다.

## 남아 있는 한계와 개선 계획

현재 API는 풀보다 객체의 수명이 길어지는 사용을 지원하지 않습니다. 향후 checked-out 객체가 남은 소멸을 명시적 오류 정책으로 바꾸고, 다양한 객체 크기와 장시간 contention을 별도 benchmark로 측정할 계획입니다.
