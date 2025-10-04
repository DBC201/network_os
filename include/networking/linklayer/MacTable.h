#ifndef MAC_TABLE_H
#define MAC_TABLE_H

#include <unordered_map>
#include "time_utils.h"

#ifndef NDEBUG
#include <iostream>
#include "mac_utils.h"
#endif

class MacTable {
    public:
    MacTable() {
    }

    void removeExpired(uint64_t timeout_ns) {
        uint64_t now = now_ns_monotonic();
        
        for (auto it=table.begin(); it!=table.end(); it++) {
            for (auto it2=it->second.begin(); it2!=it->second.end(); ) {
                if (now - it2->second >= timeout_ns) {
                    #ifndef NDEBUG
                    std::cerr << "Deleting expired entry: ";
                    std::cerr << it->first << ": ";
                    std::cerr << mac_to_str(unpack_mac_bytes(it2->first).data());
                    std::cerr << std::endl;
                    #endif
                    it2 = it->second.erase(it2);
                }
                else {
                    it2++;
                }
            }
        }
    }

    void addEntry(std::string ifname, uint64_t mac) {
        if (!table.contains(ifname)) {
            table.insert({ifname, std::unordered_map<uint64_t, uint64_t>()});
        }

        if (table.at(ifname).contains(mac)) {
            table.at(ifname).at(mac) = now_ns_monotonic();
        }
        else {
            table.at(ifname).insert({mac, now_ns_monotonic()});
        }
    }

    void removeInterface(std::string ifname) {
        table.erase(ifname);
    }

    // ifname: {mac: timestamp}
    std::unordered_map<std::string, std::unordered_map<uint64_t, uint64_t>> table;
};

#endif
