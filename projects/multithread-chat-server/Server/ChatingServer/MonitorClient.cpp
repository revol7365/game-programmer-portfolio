#include "MonitorClient.h"
#include "ChatServer.h"

MonitorClient::MonitorClient(ChatServer* server, int serverNo)
	: LanClient(), mServer(server), mServerNo(serverNo)
{
}

MonitorClient::~MonitorClient()
{
}

void MonitorClient::SendMonitorData()
{
	if (!IsConnected()) {
		mLoginSent = false;
		if (!Connect()) return;
	}

	if (!mLoginSent) {
		if (!sendLogin()) return;
		mLoginSent = true;
	}

	int timeStamp = (int)time(nullptr);
	const ProcessMonitor& monitor = mServer->GetProcessMonitor();

	// Chat Server 데이터 (30~37)
	sendData(dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN, 1, timeStamp);
	sendData(dfMONITOR_DATA_TYPE_CHAT_SERVER_CPU, monitor.GetCpuProccessPct(), timeStamp);
	sendData(dfMONITOR_DATA_TYPE_CHAT_SERVER_MEM, (int)monitor.GetPrivateMB(), timeStamp);
	sendData(dfMONITOR_DATA_TYPE_CHAT_SESSION, mServer->GetSessionCount(), timeStamp);
	sendData(dfMONITOR_DATA_TYPE_CHAT_PLAYER, mServer->GetPlayerCount(), timeStamp);
	sendData(dfMONITOR_DATA_TYPE_CHAT_UPDATE_TPS, mServer->GetRecvTPS(), timeStamp);
	sendData(dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, (int)Packet::packetMemoryPool.GetAllocCount(), timeStamp);
	sendData(dfMONITOR_DATA_TYPE_CHAT_UPDATEMSG_POOL, 0, timeStamp);
}

bool MonitorClient::sendLogin()
{
	Packet* packet = Packet::packetMemoryPool.Alloc();
	if (packet == nullptr) return false;
	packet->Init();

	// LAN_HEADER 공간 확보 (2바이트)
	packet->PutData("\0\0", sizeof(LAN_HEADER));

	WORD type = en_PACKET_SS_MONITOR_LOGIN;
	*packet << type;
	*packet << mServerNo;

	bool result = SendPacket(packet);
	if (result)
		InterlockedIncrement(&mSendCount);
	packet->SubRef();
	return result;
}

void MonitorClient::sendData(BYTE dataType, int value, int timeStamp)
{
	InterlockedIncrement(&mSendTPS);
	Packet* packet = Packet::packetMemoryPool.Alloc();
	if (packet == nullptr) return;
	packet->Init();

	// LAN_HEADER 공간 확보 (2바이트)
	packet->PutData("\0\0", sizeof(LAN_HEADER));

	WORD type = en_PACKET_SS_MONITOR_DATA_UPDATE;
	*packet << type;
	*packet << dataType;
	*packet << value;
	*packet << timeStamp;

	if (SendPacket(packet))
		InterlockedIncrement(&mSendCount);
	packet->SubRef();
}

// --- LanClient 콜백 ---

void MonitorClient::OnConnect()
{
	LOG(L"[MonitorClient] Connected to Monitor Server\n");
}

void MonitorClient::OnDisconnect()
{
	LOG(L"[MonitorClient] Disconnected from Monitor Server\n");
	mLoginSent = false;
}

void MonitorClient::OnRecv(Packet* packet)
{
	packet->SubRef();
}

void MonitorClient::OnSend(int sendsize)
{
}

void MonitorClient::OnError(int errorCode, const wchar_t* errorMessage)
{
	LOG(L"[MonitorClient] Error %d: %s\n", errorCode, errorMessage);
}
