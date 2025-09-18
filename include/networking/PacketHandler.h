#ifndef PACKET_HANDLER_H
#define PACKET_HANDLER_H

#include <sys/epoll.h>
#include <thread>
#include <mutex>

#include "linklayer/PacketSwitch.h"
#include <unix_wrapper/UnixWrapper.h>
#include <linklayer/RawSocket.h>
#include <string_utils.h>

using cpp_socket::unix_wrapper::UnixWrapper;
using cpp_socket::linklayer::RawSocket;
using cpp_socket::linklayer::PROMISCIOUS;
using cpp_utils::string_utils::split;


class PacketHandler {
    public:
    PacketHandler() {
        ep = epoll_create1(EPOLL_CLOEXEC);
    }

    void register_device(std::string ifname) {
        m.lock();
        if (!namemap.contains(ifname)) {
            RawSocket* rawSocket = new RawSocket(ifname, PROMISCIOUS, false);
            add_socket(rawSocket->get_socket());
            namemap.insert({ifname, rawSocket});
            fdmap.insert({rawSocket->get_socket(), rawSocket});
        }
        m.unlock();
    }

    void remove_socket(int sockfd) {
        m.lock();
        RawSocket* rawSocket = fdmap.at(sockfd);
        fdmap.erase(sockfd);
        std::string ifname = rawSocket->get_ifname();
        namemap.erase(ifname);
        delete rawSocket; // this calls close(fd)
        m.unlock();
    }

    void run() {

    }

    ~PacketHandler() {
        delete packetSwitch;
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
            }
            
            std::string s(buffer, r);
            std::vector<std::string> tokens = split(s, ' ');

            if (tokens[1] == "NEW") {
                register_device(tokens[0]);
            }
        }
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
                    
                } else if (e & EPOLLOUT) {
                }
            }
    
            // Grow event array if we hit capacity
            if (n == static_cast<int>(events.size())) {
                events.resize(events.size() * 2);
            }

        }
    }

    PacketSwitch* packetSwitch;

    // ifname, RawSocket*
    std::unordered_map<std::string, RawSocket*> namemap;
    
    // fd, RawSocket*
    std::unordered_map<int, RawSocket*> fdmap;

    int ep;
    std::mutex m;

};

#endif
