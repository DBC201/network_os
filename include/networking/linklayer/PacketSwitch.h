#ifndef PACKET_SWITCH_H
#define PACKET_SWITCH_H

#include "MacTable.h"
#include <net/ethernet.h>
#include <string.h>

class PacketSwitch {
    public:
    PacketSwitch() {
    }

    std::string switchPacket(std::string src_ifname, unsigned char* packet, int packet_size) {
        ether_header* header = (ether_header *) packet;
        uint64_t dest_mac = pack_mac_bytes(header->ether_dhost);
        uint64_t src_mac = pack_mac_bytes(header->ether_shost);    

        macTable.addEntry(src_ifname, src_mac);

        for (auto it=macTable.table.begin(); it!=macTable.table.end(); it++) {
            if (it->first == src_ifname) {
                continue;
            }
            
            if (it->second.contains(dest_mac)) {
                return it->first;
            }
        }

        return "";
    }
    
    MacTable macTable;
};


#endif
