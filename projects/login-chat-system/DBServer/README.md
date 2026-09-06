# DB Work Queue

게임 저장 요청을 생산자 스레드에서 큐에 넣고 단일 DB writer가 처리하는 MySQL 부하 재현 프로젝트입니다. 아이템 거래를 transaction/비transaction 모드로 각각 실행해 처리량과 원자성 구현을 비교합니다.

생성자 인자로 실행 모드를 확정한 뒤 worker를 시작하므로 기존의 시작 직후 모드 변경 data race가 없습니다. 종료 상태는 atomic이며 event handle도 정상 회수합니다. 접속 정보는 `PORTFOLIO_DB_*` 환경 변수에서 읽습니다.
