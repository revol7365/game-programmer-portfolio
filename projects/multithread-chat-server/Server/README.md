# Multi-thread Chat Server

Windows IOCP 기반 멀티스레드 채팅 서버입니다. 패킷 framing·암호화·checksum 검증, 세션 수명 관리, 섹터 이동과 주변 섹터 broadcast를 구현합니다.

이 독립 실행판은 Redis 인증 없이 프로토콜과 다중 접속을 재현하도록 구성했습니다. 로그인·Redis 일회용 세션키 검증은 `projects/login-chat-system`에서 실행합니다. 서버는 기본적으로 loopback `127.0.0.1:6000`에 바인딩됩니다.
