#ifndef DEVICE_HANDLER_H
#define DEVICE_HANDLER_H

#include <netlink/NetlinkWrapper.h>
#include <base/SocketWrapper.h>
#include <unix_wrapper/UnixWrapper.h>
#include <iomanip>

using cpp_socket::netlink::NetlinkWrapper;
using cpp_socket::netlink::LINK;
using cpp_socket::netlink::ROUTE;
using cpp_socket::base::get_syscall_error;
using cpp_socket::unix_wrapper::UnixWrapper;


std::string macToString(const unsigned char* mac_bytes, size_t length = 6) {
    std::ostringstream oss;
    for (size_t i = 0; i < length; ++i) {
        if (i != 0)
            oss << ":";
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(mac_bytes[i]);
    }
    return oss.str();
}

class DeviceHandler {
    public:
    DeviceHandler(std::string address, std::string packet_handler_address) {
        netlinkWrapper = new NetlinkWrapper(ROUTE, LINK, true);
        unixWrapper = new UnixWrapper(address, true, true);
        this->packet_handler_address = UnixWrapper::get_dest_sockaddr(packet_handler_address, true);
    }

    void run() {
        while (true) {
            std::vector<char> buf(1 << 17); // 128 KiB
            int n = netlinkWrapper->receive_wrapper(buf.data(), buf.size(), 0);
    
            if (n < 0) {
                int err = get_syscall_error();
                if (err == EINTR) {
                    continue; // since blocking call may get interrupted via kernel mid read
                }
    
                std::cout << "Error occured with code: " << err << std::endl;
                
                // TODO: Handle error better

                break;
            }
    
            // Walk all netlink messages in this datagram
            for (nlmsghdr* nh = (nlmsghdr*)buf.data(); NLMSG_OK(nh, (unsigned)n); nh = NLMSG_NEXT(nh, n)) {
                if (nh->nlmsg_type == NLMSG_ERROR) {
                    std::cout << "netlink: NLMSG_ERROR\n";

                    // TODO: handle error better

                    continue;
                }
                if (nh->nlmsg_type == NLMSG_DONE) {
                    continue;
                }
                if (nh->nlmsg_type != RTM_NEWLINK && nh->nlmsg_type != RTM_DELLINK) {
                    // Not a link event; ignore (you can subscribe to more groups and handle here)
                    continue;
                }
    
                // Header -> ifinfomsg
                auto* ifi = (ifinfomsg*)NLMSG_DATA(nh);
                int ifindex = ifi->ifi_index;
                unsigned ifflags = ifi->ifi_flags;

                if (ifi->ifi_flags & IFF_LOOPBACK) {
                    continue;
                }
    
                // Parse attributes
                char ifname[IF_NAMESIZE] = {0};
                unsigned char* mac_bytes = nullptr;
                int maclen = 0;
                int mtu = 0;
    
                int attrlen = nh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifi));
                for (rtattr* rta = (rtattr*)IFLA_RTA(ifi); RTA_OK(rta, attrlen); rta = RTA_NEXT(rta, attrlen)) {
                    switch (rta->rta_type) {
                        case IFLA_IFNAME:
                            std::snprintf(ifname, sizeof(ifname), "%s", (char*)RTA_DATA(rta));
                            break;
                        case IFLA_ADDRESS:
                            mac_bytes = (unsigned char*)RTA_DATA(rta);
                            maclen = RTA_PAYLOAD(rta);
                            break;
                        case IFLA_MTU:
                            mtu = *(int*)RTA_DATA(rta);
                            break;
                        default:
                            break;
                    }
                }

                std::string ifname_str(ifname);

                if (nh->nlmsg_type == RTM_NEWLINK) {
                    // ifname NEW mac
                    ifname_str += " NEW ";
                    ifname_str += macToString(mac_bytes, maclen);
                    ifname_str += " " + std::to_string(mtu);
                }
                else if (nh->nlmsg_type == RTM_DELLINK) {
                    //ifname DEL 
                    ifname_str += " DEL";
                }
                unixWrapper->sendto_wrapper(ifname_str.c_str(), ifname_str.size(), 0, packet_handler_address->get_sockaddr(), packet_handler_address->size());
            }
        }
    }

    ~DeviceHandler() {
        delete netlinkWrapper;
        delete unixWrapper;
        delete packet_handler_address;
    }

    private:
    NetlinkWrapper* netlinkWrapper;
    UnixWrapper* unixWrapper;
    Address* packet_handler_address;
};

#endif