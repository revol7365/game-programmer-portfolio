#pragma once
#include "Group.h"

class EchoGroup : public Group {
public:
    EchoGroup(GroupServer* server) : Group(server) {}

protected:
    void OnRecv(Player* player, Packet* packet) override;

private:
    bool handleEcho(Player* player, Packet* packet);
    bool handleHeartbeat(Player* player, Packet* packet);
};
