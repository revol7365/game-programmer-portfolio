#pragma once

#include "../NetLib/LanClient.h"
#include "../NetLib/ProcessMonitor.h"
#include "../Utils/CommonProtocol.h"

class LoginServer;

class MonitorClient : public LanClient
{
public:
	MonitorClient(LoginServer* server, int serverNo = LOGIN_SERVER_NO);
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

	LoginServer* mServer;
	int mServerNo;
	bool mLoginSent = false;

	long mSendCount = 0;
	long mSendTPS = 0;
};
