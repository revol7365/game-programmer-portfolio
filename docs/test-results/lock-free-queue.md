# Lock-Free Queue test

- 환경: Windows 11 Pro, Visual Studio 2022 17.14.12, MSVC 14.44.35207
- 구성/플랫폼: Debug x64, Release x64
- 날짜: 2026-09-07
- 빌드: PASS / PASS
- 실행: `.\scripts\Test-Core.ps1`
- 절차: single-thread FIFO 후 8 producers + 8 consumers, producer당 200,000개를 10회 반복
- 결과: 각 구성 매 round produced/consumed 1,600,000, missing 0, duplicate 0, corrupt 0
- 관측 시간: Debug 14.996초, Release 4.423초
- 로그: [lock-free-queue-20260907.txt](logs/lock-free-queue-20260907.txt)

thread 반복 생성 환경의 hazard record 회수와 장시간 메모리 상한은 검증하지 않았습니다.
