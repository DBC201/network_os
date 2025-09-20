#ifndef PACKET_HANDLER_H
#define PACKET_HANDLER_H

#include <sys/epoll.h>
#include <thread>
#include <mutex>
#include <queue>
#include <ifaddrs.h>
#include <unordered_set>
#include <linux/if.h>

#include "linklayer/PacketSwitch.h"
#include <unix_wrapper/UnixWrapper.h>
#include <linklayer/RawSocket.h>
#include <string_utils.h>
#include <base/SocketWrapper.h>

using cpp_socket::unix_wrapper::UnixWrapper;
using cpp_socket::linklayer::RawSocket;
using cpp_socket::linklayer::PROMISCIOUS;
using cpp_utils::string_utils::split_by_string;
using cpp_utils::string_utils::convert_string;
using cpp_socket::base::get_syscall_error;

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

    std::queue<Packet*> output_buffer;

    /**
     * @brief 
     * Will manage ownership and deletion of *rawSocket
     * 
     * @param rawSocket 
     * @param mtu 
     */
    Ifentry(RawSocket *rawSocket, bool loopback, bool broadcast, bool multicast, int mtu) {
        this->rawSocket = rawSocket;
        this->loopback = loopback;
        this->broadcast = broadcast;
        this->multicast = multicast;
        this->mtu = mtu;
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
            } else {
                // TODO: HANDLE ERROR(S)
            }
        }
    }

    void update_device(std::string ifname, bool loopback, bool broadcast, bool multicast, int mtu) {
        m.lock();
        if (!namemap.contains(ifname)) {
            RawSocket* rawSocket = new RawSocket(ifname, PROMISCIOUS, false);
            add_socket(rawSocket->get_socket());
            Ifentry* ifentry = new Ifentry(rawSocket, loopback, broadcast, multicast, mtu);
            namemap.insert({ifname, ifentry});
            fdmap.insert({rawSocket->get_socket(), ifentry});
        }
        else if (namemap.at(ifname)->mtu != mtu) {
            namemap.at(ifname)->mtu = mtu;
        }
        m.unlock();
    }

    void remove_device(std::string ifname) {
        m.lock();
        if (namemap.contains(ifname)) {
            Ifentry* ifentry = namemap.at(ifname);
            packetSwitch.macTable.removeInterface(ifentry->rawSocket->get_ifname());
            fdmap.erase(ifentry->rawSocket->get_socket());
            namemap.erase(ifname);
            delete ifentry;
        }
        m.unlock();
    }

    void remove_socket(int sockfd) {
        m.lock();
        if (fdmap.contains(sockfd)) {
            Ifentry* ifentry = fdmap.at(sockfd);
            fdmap.erase(sockfd);
            std::string ifname = ifentry->rawSocket->get_ifname();
            namemap.erase(ifname);
            packetSwitch.macTable.removeInterface(ifentry->rawSocket->get_ifname());
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

        packet_processor_thread.join();
        device_manager_communication_thread.join();
    }

    void update_devices() {
        ifaddrs *ifs = nullptr;
        
        if (getifaddrs(&ifs) == -1) {
            perror("getifaddrs");
            return;
        }

        for (auto *p = ifs; p; p = p->ifa_next) {
            if (!p || !p->ifa_name || !(p->ifa_flags & IFF_LOWER_UP)) {
                continue;
            }

            bool loopback = (p->ifa_flags & IFF_LOOPBACK) != 0;
            bool broadcast = (p->ifa_flags & IFF_BROADCAST) != 0;
            bool multicast = (p->ifa_flags & IFF_MULTICAST) != 0;

            std::string ifname(p->ifa_name);
            update_device(p->ifa_name, loopback, broadcast, multicast, 1500);
        }
        
        freeifaddrs(ifs);
    }

    ~PacketHandler() {
        // TODO: CLEAR MAPS AND BUFFER
        for (auto it=namemap.begin(); it!=namemap.end(); it++) {
            delete it->second;
        }
        for (auto it=fdmap.begin(); it!=fdmap.end(); it++) {
            delete it->second;
        }
    }

    private:
    void add_socket(int sockfd) {
        epoll_event ev{};
        ev.events  = EPOLLIN | EPOLLET;
        ev.data.fd = sockfd;

        if (epoll_ctl(ep, EPOLL_CTL_ADD, sockfd, &ev) == -1) {
            // Common cases: EEXIST (already added), EBADF/ENOENT (bad fd)
            // TODO: HANDLE ERROR
            return;
        }
    }

    void device_manager_communication(std::string address) {
        UnixWrapper unixWrapper(address, true, true);
        while (1) {
            char buffer[64];
            int r = unixWrapper.receive_wrapper(buffer, 64, 0);

            if (r < 0) {
                // TODO: HANDLE ERROR
            }
            else if (r == 0) {
                // TODO: HANDLE PIPE CLOSURE
                continue;
            };

            std::string s(buffer, r);
            std::vector<std::string> tokens = split_by_string(s, " ");

            if (tokens[1] == "NEW") {
                // ifname NEW <LOOPBACK,BROADCAST,MULTICAST,LOWER_UP> <mtu> <mac>

                int mtu = convert_string<int>(tokens[3]);

                if (mtu == 0) {
                    mtu = 1500;
                }

                tokens[2].erase(0, 1);
                tokens[2].erase(s.size() - 1);

                std::vector<std::string> ifflag_tokens = split_by_string(tokens[2], ",");
                std::unordered_set<std::string> s(ifflag_tokens.begin(), ifflag_tokens.end());

                if (s.contains("LOWER_UP")) {
                    update_device(tokens[0], s.contains("LOOPBACK"), s.contains("BROADCAST"), s.contains("MULTICAST"), mtu);
                } else {
                    remove_device(tokens[0]);
                }
            }
        }
    }

    int receive_packet(int fd) {
        unsigned char* packet;
        int mtu;
        std::string src_ifname;
        m.lock();
        if (fdmap.contains(fd)) {
            mtu = fdmap.at(fd)->mtu;
            src_ifname = fdmap.at(fd)->rawSocket->get_ifname();
        }
        else {
            // TODO: HANDLE UNEXPECTED BEHAVIOR
            m.unlock();
            return -1;
        }
        m.unlock();
        int frame_size = mtu + sizeof(ether_header);

        packet = new unsigned char[frame_size];
        int r = recv(fd, packet, frame_size, 0);

        if (r < 0) {
            return r;
        }

        std::string out_ifname = packetSwitch.switchPacket(src_ifname, packet, r);

        m.lock();
        if (out_ifname != "") {
            if (namemap.contains(out_ifname)) {
                Ifentry *ifentry = namemap.at(out_ifname);
                set_epollout(ifentry->rawSocket->get_socket(), true);
                ifentry->output_buffer.push(new Packet(packet, r));
            }
        }
        else {
            // BROADCAST
            for (auto it=namemap.begin(); it!=namemap.end(); it++) {
                if (it->first == src_ifname) {
                    continue;
                }
                unsigned char* packet_copy = new unsigned char[frame_size];
                memcpy(packet_copy, packet, frame_size);
                it->second->output_buffer.push(new Packet(packet_copy, r));
                set_epollout(it->second->rawSocket->get_socket(), true);
            }
            delete[] packet;
        }
        m.unlock();

        return r;
    }
    
    void packet_processor() {
        std::vector<epoll_event> events(256);

        while (true) {
            int n = epoll_wait(ep, events.data(), static_cast<int>(events.size()), -1);
            if (n < 0) {
                if (errno == EINTR) continue;
                std::cerr << "epoll_wait: " << errno << std::endl;
                break;
            }

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
                    while (true) {
                        int r = receive_packet(fd);
                        if (r < 0) {
                            int err = get_syscall_error();
                            if (err == EAGAIN || err == EWOULDBLOCK) {
                            } else {
                                // TODO: HANDLE ERROR
                            }
                            break;
                        }
                    }
                }
                
                if (e & EPOLLOUT) {
                    m.lock();
                    if (fdmap.contains(fd)){
                        Ifentry* ifentry = fdmap.at(fd);
                        while (!ifentry->output_buffer.empty()) {
                            Packet* packet = ifentry->output_buffer.front();

                            // TODO: check if ifentry exists

                            int r = ifentry->rawSocket->send_wrapper((const char*)packet->data, packet->size, 0);

                            if (r < 0) {
                                // TODO: CHECK errno to determine if packet should be dropped or not
                                // based on if write is blocked or something else
                                break;
                            }
                            ifentry->output_buffer.pop();
                        }

                        if (ifentry->output_buffer.empty()) {
                            set_epollout(ifentry->rawSocket->get_socket(), false);
                        }
                    }
                    m.unlock();
                }
            }
    
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
