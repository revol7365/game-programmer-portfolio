#pragma once
#include "Group.h"

class GroupManager;

class LoginGroup : public Group {
public:
    LoginGroup(GroupServer* server, GroupManager* manager)
        : Group(server), mManager(manager) {}

protected:
    void OnRecv(Player* player, Packet* packet) override;

private:
    bool handleLogin(Player* player, Packet* packet);

    GroupManager* mManager;
};
