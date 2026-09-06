#pragma once

#include <atomic>
#include <string>
#include <cpp_redis/cpp_redis>

#pragma comment(lib, "cpp_redis.lib")
#pragma comment(lib, "tacopie.lib")

class RedisConnecter {
public:
	RedisConnecter() = default;
	~RedisConnecter();

	bool Connect(const std::string& host = "127.0.0.1", int port = 6379);
	void Disconnect();
	bool IsConnected() const;

	bool Set(const std::string& key, const std::string& value, int ttlSeconds);
	bool CompareAndDel(const std::string& key, const std::string& expected);

private:
	cpp_redis::client& GetThreadClient();

	std::atomic<bool> mConnected{ false };
	std::string mHost;
	int mPort = 0;
};
