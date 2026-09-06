#include "GroupManager.h"

GroupManager::GroupManager(GroupServer* server, int echoShardCount)
    : mServer(server)
{
    if (echoShardCount < 1) echoShardCount = 1;

    mLoginGroup = std::make_unique<LoginGroup>(server, this);

    mEchoShards.reserve(echoShardCount);
    for (int i = 0; i < echoShardCount; ++i) {
        mEchoShards.emplace_back(std::make_unique<EchoGroup>(server));
    }
}
