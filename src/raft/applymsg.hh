#ifndef APPLYMSG_HH
#define APPLYMSG_HH

#include "RaftRpcs.pb.h"

class ApplyMsg {
    RaftRpcsProto::LogEntry le_;
public:
    ApplyMsg(RaftRpcsProto::LogEntry le) :le_(le){}
};


#endif