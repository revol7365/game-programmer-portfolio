#pragma once
#include "BaseServer.h"

class LanServer : public BaseServer
{
public:
	LanServer() : BaseServer(LAN_PORT) {}
	virtual ~LanServer() {}
};
