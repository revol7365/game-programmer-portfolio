#pragma once

#include "BaseServer.h"

class NetServer : public BaseServer {

public:
	NetServer(int port = DEFAULT_PORT) : BaseServer(port) {}
	virtual ~NetServer() {}


};
