#ifndef CONFIG_HH
#define CONFIG_HH

#include <chrono>

using namespace std::chrono;

namespace RaftConfig{
// 心跳间隔
duration heartBeatInterv = milliseconds(200);
// 选举间隔时间下限和上限
duration electionIntervLow = milliseconds(1000);
duration electionIntervHigh = milliseconds(1500);

// requestVote RPC 的超时时间
duration requestVoteTimeout = milliseconds(500);

// apply的时间间隔
duration applyInterv = milliseconds(500);
};

namespace ServerConfig {
duration consensusTimeout = milliseconds(500);
}

class NodeConfig{
public:
    NodeConfig(std::string fileName){}
    ~NodeConfig(){}

    std::vector<std::pair<std::string, std::string>> confItems;
};

#endif