# Memory Pool test

- 환경: Windows 11 Pro, Visual Studio 2022 17.14.12, MSVC 14.44.35207
- 구성/플랫폼: Debug x64, Release x64
- 날짜: 2026-09-07
- 빌드: PASS / PASS
- 실행: `.\scripts\Test-Core.ps1`
- 결과: FunctionTest PASS, MultiThreadTest PASS, LifetimeTest PASS(`live=0`)
- 부하: 16 threads × 100,000 alloc/free
- 관측 benchmark: Debug 2,000,000 operations 317.311 ms, Release 245.063 ms
- 로그: [memory-pool-20260907.txt](logs/memory-pool-20260907.txt)

benchmark 값은 해당 PC의 단일 재현 결과이며 일반 성능 보장으로 사용하지 않습니다. 장시간 soak와 다양한 객체 크기는 검증하지 않았습니다.
