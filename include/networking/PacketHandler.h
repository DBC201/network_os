#ifndef PACKET_HANDLER_H
#define PACKET_HANDLER_H


#include "linklayer/PacketSwitch.h"
#include <unix_wrapper/UnixWrapper.h>

using cpp_socket::unix_wrapper::UnixWrapper;


class PacketHandler {
    public:
    PacketHandler(std::string address) {
        unixWrapper = new UnixWrapper(address, true, true);
    }
    
    void run() {

    }

    ~PacketHandler() {
        delete unixWrapper;
        delete packetSwitch;
    }

    private:
    UnixWrapper* unixWrapper;
    PacketSwitch* packetSwitch;
};

#endif
