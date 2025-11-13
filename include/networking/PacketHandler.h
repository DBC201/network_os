#ifndef PACKET_HANDLER_H
#define PACKET_HANDLER_H

#include <sys/epoll.h>
#include <thread>
#include <mutex>
#include <queue>
#include <ifaddrs.h>
#include <unordered_set>
#include <linux/if_link.h>
#include <sys/ioctl.h>
#include <linux/sockios.h>
#include <cstring>

#include "linklayer/PacketSwitch.h"
#include <unix_wrapper/UnixWrapper.h>
#include <linklayer/RawSocket.h>
#include <string_utils.h>
#include <base/SocketWrapper.h>
#include <networking/linklayer/mac_utils.h>

using cpp_socket::unix_wrapper::UnixWrapper;
using cpp_socket::linklayer::RawSocket;
using cpp_socket::linklayer::PROMISCIOUS;
using cpp_utils::string_utils::split;
using cpp_utils::string_utils::convert_string;

struct Packet {
    unsigned char* data;
    int size;

    /**
     * @brief
     * Will manage ownership and deletion of *data
     * 
     * @param data 
     * @param size 
     */
    Packet(unsigned char* data, int size) {
        this->data = data;
        this->size = size;
    }

    ~Packet() {
        delete[] data;
    }
};


struct Ifentry {
    RawSocket* rawSocket;
    bool loopback;
    bool broadcast;
    bool multicast;
    int mtu;
    uint64_t mac;

    std::queue<Packet*> output_buffer;

    /**
     * @brief 
     * Will manage ownership and deletion of *rawSocket
     * 
     * @param rawSocket 
     * @param loopback 
     * @param broadcast 
     * @param multicast 
     * @param mtu
     * @param mac 
     */
    Ifentry(RawSocket *rawSocket, bool loopback, bool broadcast, bool multicast, int mtu, uint64_t mac) {
        this->rawSocket = rawSocket;
        this->loopback = loopback;
        this->broadcast = broadcast;
        this->multicast = multicast;
        this->mtu = mtu;
        this->mac = mac;
    }

    ~Ifentry() {
        delete rawSocket;
        
        while (!output_buffer.empty()) {
            Packet* curr = output_buffer.front();
            output_buffer.pop();
            delete curr;
        }
    }
};

class PacketHandler {
    public:
    PacketHandler() {
        ep = epoll_create1(EPOLL_CLOEXEC);
        update_devices();
    }

    void update_device(std::string ifname, bool loopback, bool broadcast, bool multicast, int mtu, uint64_t mac) {
        m.lock();
        if (!namemap.contains(ifname)) {
            RawSocket* rawSocket = new RawSocket(ifname, PROMISCIOUS, false);
            
            if (!loopback) {
                // do not register loopback for epoll
                register_socket_epoll(rawSocket->get_socket());
            }
            
            // To read from dummy interfaces while testing, this needs to be disabled
            rawSocket->set_ignore_outgoing(1);

            // Disable pause frames
            // rawSocket->set_pause_frames(0);

            int rc = 0;
            // 1) Turn off RX/TX checksum offload
            // rc |= rawSocket->ethtool_set_value(ETHTOOL_SRXCSUM, 0);
            // rc |= rawSocket->ethtool_set_value(ETHTOOL_STXCSUM, 0);

            // 2) Turn off GRO/LRO
            // rc |= rawSocket->ethtool_set_value(ETHTOOL_SGRO, 0);       // GRO
            // LRO might be only in flags on older kernels
            // rc |= rawSocket->ethtool_clear_flags(ETH_FLAG_LRO);

            // 3) Turn off GSO/TSO (generic + specific)
            // rc |= rawSocket->ethtool_set_value(ETHTOOL_SGSO, 0);       // generic segmentation offload
            // rc |= rawSocket->ethtool_set_value(ETHTOOL_STSO, 0);       // TCP segmentation offload

            #ifndef NDEBUG
            std::cerr << "Adding " << rawSocket->get_ifname() << " " << loopback << " " << broadcast << " " << multicast << " " << mtu << std::endl;
            #endif

            Ifentry* ifentry = new Ifentry(rawSocket, loopback, broadcast, multicast, mtu, mac);
            namemap.insert({ifname, ifentry});
            fdmap.insert({rawSocket->get_socket(), ifentry});
        }
        else {
            namemap.at(ifname)->mtu = mtu;
        }
        m.unlock();
    }

    void remove_device(std::string ifname) {
        m.lock();
        if (namemap.contains(ifname)) {
            Ifentry* ifentry = namemap.at(ifname);
            #ifndef NDEBUG
            std::cerr << "Removing " << ifentry->rawSocket->get_ifname() << std::endl;
            #endif
            packetSwitch.macTable.removeInterface(ifentry->rawSocket->get_ifname());
            fdmap.erase(ifentry->rawSocket->get_socket());
            namemap.erase(ifname);
            delete ifentry;
        }
        m.unlock();
    }

    void run(std::string address) {
        std::thread packet_processor_thread([&]() {
            packet_processor();
        });

        std::thread device_manager_communication_thread([&]() {
            device_manager_communication(address);
        });
        
        #ifndef NDEBUG
        std::thread debug_info_thread([&]() {
            while (true) {
                print_mactable();
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        });

        debug_info_thread.join();
        #endif

        packet_processor_thread.join();
        device_manager_communication_thread.join();
    }

    void update_devices() {
        ifaddrs *ifs = nullptr;

        if (getifaddrs(&ifs) == -1) {
            perror("Error updating devices");
            return;
        }

        // Reuse one socket for all ioctls
        int sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == -1) {
            perror("socket");
            freeifaddrs(ifs);
            return;
        }

        for (auto *p = ifs; p; p = p->ifa_next) {
            // IF_LOWER_UP 1 << 16
            if (!p || !p->ifa_name || !(p->ifa_flags & (1 << 16))) {
                continue;
            }

            bool loopback  = (p->ifa_flags & IFF_LOOPBACK)   != 0;
            bool broadcast = (p->ifa_flags & IFF_BROADCAST)  != 0;
            bool multicast = (p->ifa_flags & IFF_MULTICAST)  != 0;

            // Get MTU
            int mtu = 1500; // fallback
            struct ifreq ifr{};
            std::strncpy(ifr.ifr_name, p->ifa_name, IFNAMSIZ - 1);
            if (ioctl(sock, SIOCGIFMTU, &ifr) == 0) {
                mtu = ifr.ifr_mtu;
            }

            // Get MAC (only meaningful for Ethernet-like devices)
            std::string mac;
            std::memset(&ifr, 0, sizeof(ifr));
            std::strncpy(ifr.ifr_name, p->ifa_name, IFNAMSIZ - 1);
            if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
                if (ifr.ifr_hwaddr.sa_family == 1) {
                    const unsigned char* hw = reinterpret_cast<unsigned char*>(ifr.ifr_hwaddr.sa_data);
                    char buf[18];
                    std::snprintf(buf, sizeof(buf),
                                "%02x:%02x:%02x:%02x:%02x:%02x",
                                hw[0], hw[1], hw[2], hw[3], hw[4], hw[5]);
                    mac.assign(buf);
                }
            }

            // assume update_device(name, loopback, broadcast, multicast, mtu, mac)
            update_device(p->ifa_name, loopback, broadcast, multicast, mtu, pack_mac_str(mac));
        }

        close(sock);
        freeifaddrs(ifs);
    }

    void print_mactable() {
        m.lock();
        std::cerr << "---------------" << std::endl;
        for (auto it=packetSwitch.macTable.table.begin(); it!=packetSwitch.macTable.table.end(); it++) {
            std::cerr << it->first << std::endl;
            for (auto it2=it->second.begin(); it2!=it->second.end(); it2++) {
                std::vector<unsigned char> bytes = unpack_mac_bytes(it2->first);
                std::cerr << "\t" << mac_to_str(bytes.data()) << std::endl;
            }
        }
        std::cerr << "---------------" << std::endl;
        m.unlock();
    }

    ~PacketHandler() {
        for (auto it=namemap.begin(); it!=namemap.end(); it++) {
            delete it->second;
        }
        namemap.clear();
        fdmap.clear();
        if (ep >= 0) {
            close(ep);
        }
    }

    private:
    /**
     * @brief register the socket for epoll (not thread safe)
     * 
     * @param sockfd 
     */
    void register_socket_epoll(int sockfd) {
        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLET;
        ev.data.fd = sockfd;

        if (epoll_ctl(ep, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
            perror("Error adding socket to epoll");
            // Common cases: EEXIST (already added), EBADF/ENOENT (bad fd)
            // TODO: HANDLE ERROR
            return;
        }
    }

    /**
     * @brief removes the socket (not thread safe)
     * 
     * @param sockfd 
     */
    void remove_socket(int sockfd) {
        if (fdmap.contains(sockfd)) {
            Ifentry* ifentry = fdmap.at(sockfd);
            #ifndef NDEBUG
            std::cerr << "Removing " << ifentry->rawSocket->get_ifname() << std::endl;
            #endif
            fdmap.erase(sockfd);
            std::string ifname = ifentry->rawSocket->get_ifname();
            namemap.erase(ifname);
            packetSwitch.macTable.removeInterface(ifentry->rawSocket->get_ifname());
            delete ifentry;
        }
    }

    void set_epollout(int fd, bool has_output) {
        epoll_event ev{};
        ev.events = EPOLLIN | EPOLLET;
        if (has_output) {
            ev.events |= EPOLLOUT;
        }
        ev.data.fd = fd;
    
        // First try to modify (most common case)
        if (epoll_ctl(ep, EPOLL_CTL_MOD, fd, &ev) == -1) {
            if (errno == ENOENT) {
                // fd not registered
                // if (epoll_ctl(ep, EPOLL_CTL_ADD, fd, &ev) == -1) {
                //     // TODO: HANDLE ERROR
                // }
                perror("Tried to set epollout for unregistered fd");
            } else {
                // TODO: HANDLE ERROR(S)
                perror("Error setting epollout for fd");
            }
        }
    }

    void device_manager_communication(std::string address) {
        UnixWrapper unixWrapper(address, true, true);
        while (1) {
            char buffer[64];
            int r = unixWrapper.receive_wrapper(buffer, 64, 0);

            if (r < 0) {
                perror("Error communicating with device manager");
            }
            else if (r == 0) {
                std::cerr << "Device manager pipe closed unexpectedly." << std::endl;
                break;
            };

            std::string message(buffer, r);
            std::vector<std::string> tokens = split(message, ' ');

            if (tokens[1] == "NEW") {
                // ifname NEW <LOOPBACK,BROADCAST,MULTICAST,LOWER_UP> <mtu> <mac>
                
                #ifndef NDEBUG
                std::cerr << message << std::endl;
                #endif

                int mtu = convert_string<int>(tokens[3]);

                if (mtu == 0) {
                    mtu = 1500;
                }

                tokens[2].erase(0, 1);
                tokens[2].erase(tokens[2].size() - 1);

                std::vector<std::string> ifflag_tokens = split(tokens[2], ',');
                std::unordered_set<std::string> ifflag_set(ifflag_tokens.begin(), ifflag_tokens.end());

                if (ifflag_set.contains("LOWER_UP")) {
                    update_device(
                        tokens[0], ifflag_set.contains("LOOPBACK"), 
                        ifflag_set.contains("BROADCAST"), ifflag_set.contains("MULTICAST"),
                        mtu, pack_mac_str(tokens[4]));
                } else {
                    remove_device(tokens[0]);
                }
            }
        }
    }

    /**
     * @brief receive a single packet from fd (not thread safe)
     * 
     * @param fd 
     * @return boolean (Returns false if fd is not readable) 
     */
    bool receive_packet(int fd) {
        unsigned char* packet;
        int mtu;
        std::string src_ifname;
        if (fdmap.contains(fd)) {
            mtu = fdmap.at(fd)->mtu;
            src_ifname = fdmap.at(fd)->rawSocket->get_ifname();
        }
        else {
            return false;
        }

        int frame_size = sizeof(ether_header) + mtu;

        packet = new unsigned char[frame_size];
        int r = recv(fd, packet, frame_size, 0);

        if (r < 0) {
            if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
                remove_socket(fd);
            }
            delete[] packet;
            return false;
        }

        std::string out_ifname = packetSwitch.switchPacket(src_ifname, packet, r);

        if (out_ifname == "") {
            // UNICAST FLOODING
            for (auto it=namemap.begin(); it!=namemap.end(); it++) {
                if (it->first == src_ifname || it->second->loopback) {
                    continue;
                }
                unsigned char* packet_copy = new unsigned char[r];
                memcpy(packet_copy, packet, r);
                it->second->output_buffer.push(new Packet(packet_copy, r));
                set_epollout(it->second->rawSocket->get_socket(), true);
            }
            delete[] packet;
        } else if (out_ifname == "DROP") {
            delete[] packet;
        }
        else if (namemap.contains(out_ifname)){
            Ifentry *ifentry = namemap.at(out_ifname);
            set_epollout(ifentry->rawSocket->get_socket(), true);
            ifentry->output_buffer.push(new Packet(packet, r));
        }
        else {
            #ifndef NDEBUG
            std::cerr << "Switching error to unknown ifname: " << out_ifname << std::endl;
            #endif
            delete[] packet;
        }

        return true;
    }
    
    void packet_processor() {
        std::vector<epoll_event> events(256);

        while (true) {
            int n = epoll_wait(ep, events.data(), static_cast<int>(events.size()), -1);
            if (n < 0) {
                if (errno == EINTR) continue;
                perror("epoll_wait threw an error");
                break;
            }

            m.lock();
            for (int i = 0; i < n; ++i) {
                int fd = events[i].data.fd;
                uint32_t e = events[i].events;
    
                if (e & (EPOLLERR | EPOLLHUP)) {
                    // Error or hangup: close and remove
                    // (Kernel removes it from epoll automatically when fd is closed)
                    remove_socket(fd);
                    continue;
                }
    
                if (e & EPOLLIN) {
                    while (receive_packet(fd)) {
                    }
                }
                
                if (e & EPOLLOUT) {
                    if (fdmap.contains(fd)){
                        Ifentry* ifentry = fdmap.at(fd);
                        while (!ifentry->output_buffer.empty()) {
                            Packet* packet = ifentry->output_buffer.front();
                            int r = ifentry->rawSocket->send_wrapper((const char*)packet->data, packet->size, 0);

                            if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                                delete packet;
                                remove_socket(fd);
                                break;
                            }
                            else if (r < 0) {
                                break;
                            }
                            ifentry->output_buffer.pop();
                            delete packet;
                        }

                        if (ifentry->output_buffer.empty()) {
                            set_epollout(ifentry->rawSocket->get_socket(), false);
                        }
                    }
                }
            }
            m.unlock();
    
            // Grow event array if we hit capacity
            if (n == static_cast<int>(events.size())) {
                events.resize(events.size() * 2);
            }

        }
    }

    PacketSwitch packetSwitch;

    // ifname, ifentry*
    std::unordered_map<std::string, Ifentry*> namemap;
    
    // fd, RawSocket*
    std::unordered_map<int, Ifentry*> fdmap;

    int ep;
    std::mutex m;
};

#endif
