#include "RedisConnecter.h"
#include "../Netlib/Log.h"

RedisConnecter::~RedisConnecter()
{
	Disconnect();
}

cpp_redis::client& RedisConnecter::GetThreadClient()
{
	thread_local cpp_redis::client tClient;
	thread_local bool tConnected = false;

	if (!tConnected && mConnected.load())
	{
		try
		{
			tClient.connect(mHost, mPort, [this](const std::string& host, std::size_t port, cpp_redis::client::connect_state status) {
				if (status == cpp_redis::client::connect_state::dropped || status == cpp_redis::client::connect_state::failed)
				{
					mConnected = false;
					LOG(L"[Redis] Disconnected. host=%S, port=%zu", host.c_str(), port);
				}
			});
			tConnected = true;
		}
		catch (const std::exception& e)
		{
			LOG(L"[Redis] Thread connect failed: %S", e.what());
		}
	}

	return tClient;
}

bool RedisConnecter::Connect(const std::string& host, int port)
{
	mHost = host;
	mPort = port;

	try
	{
		// 메인 스레드에서 연결 테스트
		cpp_redis::client testClient;
		testClient.connect(host, port);
		testClient.disconnect();

		mConnected = true;
		LOG(L"[Redis] Ready. host=%S, port=%d", host.c_str(), port);
		return true;
	}
	catch (const std::exception& e)
	{
		LOG(L"[Redis] Connect failed: %S", e.what());
		mConnected = false;
		return false;
	}
}

void RedisConnecter::Disconnect()
{
	mConnected = false;
	LOG(L"[Redis] Disconnected");
}

bool RedisConnecter::IsConnected() const
{
	return mConnected.load();
}

bool RedisConnecter::Set(const std::string& key, const std::string& value, int ttlSeconds)
{
	try
	{
		auto& client = GetThreadClient();
		client.setex(key, ttlSeconds, value);
		client.sync_commit();
		return true;
	}
	catch (const std::exception& e)
	{
		LOG(L"[Redis] SETEX failed: %S", e.what());
		return false;
	}
}

bool RedisConnecter::Get(const std::string& key, std::string& outValue)
{
	try
	{
		auto& client = GetThreadClient();
		bool success = false;
		client.get(key, [&](cpp_redis::reply& reply) {
			if (reply.is_string())
			{
				outValue = reply.as_string();
				success = true;
			}
		});
		client.sync_commit();
		return success;
	}
	catch (const std::exception& e)
	{
		LOG(L"[Redis] GET failed: %S", e.what());
		return false;
	}
}
