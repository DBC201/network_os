#ifndef MAC_TABLE_H
#define MAC_TABLE_H

#include <string>
#include <unordered_map>
#include <linklayer/RawSocket.h>

uint64_t pack_mac_bytes(const unsigned char* unpacked) {
    uint64_t packed = 0;

    for (int i = 0; i < 6; i++) {
        packed |= static_cast<uint64_t>(unpacked[i]) << (40 - 8*i);
    }

    return packed;
}

class MacTable {
    public:
    MacTable() {

    }

    void addEntry(std::string ifname, uint64_t mac) {
        if (!table.contains(ifname)) {
            table.insert({ifname, std::unordered_map<uint64_t, unsigned int>()});
        }

        // TODO: TIMESTAMP
        if (table.at(ifname).contains(mac)) {
            
        }
        else {
            table.at(ifname).insert({mac, 0});
        }
    }

    void removeInterface(std::string ifname) {
        table.erase(ifname);
    }

    // ifname: {mac: timestamp}
    std::unordered_map<std::string, std::unordered_map<uint64_t, unsigned int>> table;
};

#endif
