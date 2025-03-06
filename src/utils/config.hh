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

// 
duration requestVoteTimeout = milliseconds(500);

};

#endif