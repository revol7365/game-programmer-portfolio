#pragma once

#include "../NetLib/LanClient.h"
#include "../Utils/CommonProtocol.h"
#include "../NetLib/ProcessMonitor.h"

class ChatServer;

class MonitorClient : public LanClient
{
public:
	MonitorClient(ChatServer* server, int serverNo = CHAT_SERVER_NO);
	virtual ~MonitorClient();

	void SendMonitorData();
	long GetSendTPS() const { return mSendTPS; }
	void UpdateTPS() { mSendTPS = InterlockedExchange(&mSendCount, 0); }

private:
	bool sendLogin();
	void sendData(BYTE dataType, int value, int timeStamp);

	// LanClient 콜백
	void OnConnect() override;
	void OnDisconnect() override;
	void OnRecv(Packet* packet) override;
	void OnSend(int sendsize) override;
	void OnError(int errorCode, const wchar_t* errorMessage) override;

	ChatServer* mServer;
	int mServerNo;
	bool mLoginSent = false;

	volatile long mSendCount = 0;
	volatile long mSendTPS = 0;
};
