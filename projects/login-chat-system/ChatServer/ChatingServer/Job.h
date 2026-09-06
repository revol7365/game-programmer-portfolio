//#pragma once
//
//#include <wtypes.h>
//#include "../Network/Packet.h"
//
//enum class EJobType {
//	CLIENT_JOIN,
//	CLIENT_LEAVE,
//	PACKET,
//	CHECK_HEAR_BEAT
//};
//
//class Job
//{
//	EJobType mType;
//	uint64_t mSessionID;
//	Packet* mPacket;
//
//public:
//
//	Job() = default;
//
//	void Clear()
//	{
//		mType = EJobType::PACKET;
//		mSessionID = 0;
//		mPacket = nullptr;
//	}
//
//	void SetType(EJobType type) { mType = type; }
//	void SetSessionID(uint64_t sessionID) { mSessionID = sessionID; }
//	void SetPacket(Packet* packet) { mPacket = packet; }
//
//	EJobType GetType() const { return mType; }
//	uint64_t GetSessionID() const { return mSessionID; }
//	Packet* GetPacket() { return mPacket; }
//
//	inline static MemoryPoolTLS<Job> jobMemoryPool{ 1024 * 10 };
//};