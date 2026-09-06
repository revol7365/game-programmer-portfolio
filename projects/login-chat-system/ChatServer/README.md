# Authenticated Chat Server

로그인 서버가 MySQL 계정을 확인하고 Redis에 60초 session key를 등록합니다. 채팅 서버는 Lua compare-and-delete로 키를 한 번만 소비한 뒤 채팅 접속을 허용합니다.

DB와 Redis 주소는 환경 변수로 주입하며 저장소에는 자격 증명이 없습니다. 인증되지 않은 재사용 요청은 기존 정상 세션을 끊지 않고 거절됩니다.
