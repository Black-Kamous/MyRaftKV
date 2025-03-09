#ifndef APPLYMSG_HH
#define APPLYMSG_HH

#include "RaftRpcs.pb.h"

class ApplyMsg {
public:
    bool commandValid;
    std::string command;
    int commandIndex;
    bool snapshotValid;
    std::string snapshot;
    int snapshotTerm;
    int snapshotIndex;
public:
    ApplyMsg()
        : commandValid(false),
        command(),
        commandIndex(-1),
        snapshotValid(false),
        snapshotTerm(-1),
        snapshotIndex(-1)
    {

    };
};


#endif