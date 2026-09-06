# 최초 실패와 수정 기록

원본 저장소와 브랜치는 수정하지 않았습니다. 아래 변경은 포트폴리오 복사본에만 적용했습니다.

## MemoryPool

- 최초: 기본 benchmark가 짧은 시간에 2 GiB 이상을 사용하고 완료되지 않음.
- 최초: TLS lock-free 구현은 16개 worker thread 종료 후 3,200개 객체가 풀 소멸 시 남음.
- 수정: Windows x64 `SLIST` free-list로 변경해 thread-local cache 수명 문제를 제거.
- 검증: Debug/Release에서 기능, 16스레드 반복, 소멸 후 live=0 통과.

## LockFreeQueue

- 최초: 잘못된 `InterlockedCompareExchange64` 캐스팅으로 Debug/Release 컴파일 실패.
- 최초: 임시 컴파일 수정 뒤 size mismatch와 debug break 발생.
- 최초: 최신 stress test는 10/10회 데이터 손실을 출력하면서도 종료 코드 0을 반환.
- 수정: Michael–Scott MPMC 알고리즘과 hazard pointer 회수를 구현하고 실패 종료 코드를 연결.
- 검증: Debug/Release 각 1,600,000개 × 10회, 누락·중복·손상 0.

## IOCP servers

- 최초: Release 전용 cpp_redis/tacopie/MySQL library와 Debug CRT/iterator 설정 충돌.
- 수정: 서버 Debug ABI를 외부 library와 일치시키고 최적화 해제/PDB 생성을 유지.
- 최초: 인증되지 않은 replay 연결이 Redis 검증 전에 기존 정상 세션을 disconnect.
- 수정: compare-and-delete 성공 뒤에만 중복 로그인 세션을 교체.
- 최초: Group Echo 주석은 72-byte login payload였지만 구현은 설명 없는 4 bytes를 추가 요구.
- 수정: 구현과 probe를 72 bytes로 통일.
- 최초: DBServer mode를 worker 시작 뒤 변경하고 종료 flag를 volatile로 공유.
- 수정: mode를 생성자에서 확정하고 종료 flag를 atomic으로 변경, event handle 회수 추가.
- 최초: DBServer SQL 문자열의 64-bit slot 서식과 LoginServer monitor 평균값 형식이 실제 자료형과 불일치.
- 수정: `int64_t` 계산과 SQL 서식을 일치시켜 잘림과 가변 인자 해석 오류를 제거.

## 공개 저장소 정리

- DB·Redis 주소와 포트는 환경 변수로 분리.
- DB 비밀번호, 외부 IP, Discord webhook 제거.
- crash dump는 로컬 `.dmp` 생성만 수행.
- 중복된 외부 library를 `third_party/lib` 한 벌로 통합하고 Git LFS로 관리.
- 5개 프로젝트를 `projects/` 아래 의미가 드러나는 이름으로 이동하고 통합 솔루션·스크립트 경로를 함께 갱신.
- 출처를 확인할 수 없던 Redis/MySQL 정적 바이너리를 제외하고 cpp_redis 4.3.1, tacopie 3.2.0을 공식 source에서 `/MD`로 다시 빌드. DBServer는 MySQL 8.0.44 동적 client로 전환.
- 외부 라이브러리의 버전, upstream commit, license, SHA-256 manifest를 `third_party`에 기록.
