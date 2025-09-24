#include <netlink/NetlinkWrapper.h>
#include <linux/netlink.h>
#include <os/shell_utils.h>

using cpp_socket::netlink::NetlinkWrapper;
using cpp_socket::netlink::LINK;
using cpp_socket::netlink::ROUTE;
using cpp_socket::netlink::UNICAST;

void rta_add(struct nlmsghdr* nlh, size_t maxlen, int type, const void* data, size_t len) {
    size_t nlmsg_len_aligned = NLMSG_ALIGN(nlh->nlmsg_len);
    struct rtattr* rta = (struct rtattr*)((char*)nlh + nlmsg_len_aligned);
    size_t rta_len = RTA_LENGTH(len);
    if (nlmsg_len_aligned + RTA_ALIGN(rta_len) > maxlen) {
        throw std::runtime_error("No rta space.");    
    }
    rta->rta_type = type;
    rta->rta_len  = rta_len;
    memcpy(RTA_DATA(rta), data, len);
    nlh->nlmsg_len = nlmsg_len_aligned + RTA_ALIGN(rta_len);
}

std::string flags_to_str(unsigned int f) {
    struct Bit { unsigned int bit; const char* name; };
    static const Bit bits[] = {
        { IFF_UP, "UP" }, { IFF_BROADCAST, "BROADCAST" }, { IFF_DEBUG, "DEBUG" },
        { IFF_LOOPBACK, "LOOPBACK" }, { IFF_POINTOPOINT, "POINTOPOINT" },
        { IFF_NOTRAILERS, "NOTRAILERS" }, { IFF_RUNNING, "RUNNING" },
        { IFF_NOARP, "NOARP" }, { IFF_PROMISC, "PROMISC" },
        { IFF_ALLMULTI, "ALLMULTI" }, { IFF_MASTER, "MASTER" },
        { IFF_SLAVE, "SLAVE" }, { IFF_MULTICAST, "MULTICAST" },
        { IFF_PORTSEL, "PORTSEL" }, { IFF_AUTOMEDIA, "AUTOMEDIA" },
        { IFF_DYNAMIC, "DYNAMIC" }, { 1 << 16, "LOWER_UP" },
    };
    std::string out;
    for (const auto& b : bits) {
        if (f & b.bit) {
            if (!out.empty()) out += ' ';
            out += b.name;
        }
    }
    return out;
}


void set_interface_up(NetlinkWrapper* netlinkWrapper, std::string ifname, bool up) {
    char buf[4096];
    memset(buf, 0, sizeof(buf));
    auto* nlh = (nlmsghdr*)buf;
    auto* ifi = (ifinfomsg*)NLMSG_DATA(nlh);

    uint32_t seq = 1;

    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(*ifi));
    nlh->nlmsg_type  = RTM_NEWLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK;
    nlh->nlmsg_seq   = seq;
    nlh->nlmsg_pid   = getpid();

    ifi->ifi_family = AF_UNSPEC;
    ifi->ifi_index  = if_nametoindex(ifname.c_str());

    // Specify which bits we want to change:
    unsigned int change = IFF_UP | IFF_PROMISC;
    ifi->ifi_change = change;

    unsigned int flags = 0x00;

    if (up) {
        // Desired state:
        flags = IFF_UP | IFF_PROMISC;
    }
    
    ifi->ifi_flags = flags;

    try {
        rta_add(nlh, sizeof(buf), IFLA_IFNAME, ifname.c_str(), ifname.size() + 1);

        int one = 1;
        rta_add(nlh, sizeof(buf), IFLA_PROMISCUITY, &one, sizeof(one));
    } catch (std::exception& e) {
        std::cerr << "Error setting rta flags: " << e.what() << std::endl;
        return;
    }

    sockaddr_nl kernel{};
    kernel.nl_family = AF_NETLINK;

    int r = netlinkWrapper->sendto_wrapper(buf, nlh->nlmsg_len, 0, (sockaddr*)&kernel, sizeof(kernel));

    if (r < 0) {
        perror("Error sending flag to netlink");
    }

    while (true) {
        r = netlinkWrapper->receive_wrapper(buf, sizeof(buf), 0);

        if (r < 0 && errno == EINTR) {
            continue;
        }
        else if (r < 0) {
            perror("Error receiving confirmation from netlink.");
            return;
        }

        for (nlmsghdr* nh = (nlmsghdr*)buf; NLMSG_OK(nh, (unsigned)r); nh = NLMSG_NEXT(nh, r)) {
            if (nlh->nlmsg_seq != seq) continue;
            if (nlh->nlmsg_type == NLMSG_ERROR) {
                struct nlmsgerr* err = (struct nlmsgerr*)NLMSG_DATA(nlh);
                if (err->error == 0) {
                    return;    
                }
                errno = -err->error;
                perror("netlink NLMSG_ERROR");
                return;
            }
            if (nlh->nlmsg_type == NLMSG_DONE) {
                return;
            }
        }
    }
}

void list_interfaces(NetlinkWrapper* netlinkWrapper) {
    char reqbuf[NLMSG_SPACE(sizeof(ifinfomsg))] = {};
    nlmsghdr* nlh = (nlmsghdr*)reqbuf;
    ifinfomsg* ifi = (ifinfomsg*)NLMSG_DATA(nlh);

    uint32_t seq = 1;

    nlh->nlmsg_len   = NLMSG_LENGTH(sizeof(*ifi));
    nlh->nlmsg_type  = RTM_GETLINK;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    nlh->nlmsg_seq   = seq;
    nlh->nlmsg_pid   = getpid();

    ifi->ifi_family = AF_UNSPEC;

    sockaddr_nl kernel{};
    kernel.nl_family = AF_NETLINK;

    int r = netlinkWrapper->sendto_wrapper(reinterpret_cast<const char*>(nlh), nlh->nlmsg_len, 0, (sockaddr*)&kernel, sizeof(kernel));

    if (r < 0) {
        perror("Error communicating with netlink");
        return;   
    }

    char buf[8192];
    bool done = false;

    while (!done) {
        ssize_t n = netlinkWrapper->receive_wrapper(buf, sizeof(buf), 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("Error receiving from netlink");
            break;
        }

        for (nlmsghdr* hdr = (nlmsghdr*)buf; NLMSG_OK(hdr, (unsigned)n); hdr = NLMSG_NEXT(hdr, n)) {
            if (hdr->nlmsg_seq != seq) continue;

            if (hdr->nlmsg_type == NLMSG_DONE) { done = true; break; }

            if (hdr->nlmsg_type == NLMSG_ERROR) {
                auto* err = (nlmsgerr*)NLMSG_DATA(hdr);
                if (err->error == 0) {
                    done = true; break;
                }
                errno = -err->error;
                perror("netlink NLMSG_ERROR");
                done = true;
                break;
            }

            if (hdr->nlmsg_type != RTM_NEWLINK) continue;

            auto* msg = (ifinfomsg*)NLMSG_DATA(hdr);
            int len = IFLA_PAYLOAD(hdr);

            // Parse attributes to find IFLA_IFNAME (and optionally MTU, etc.)
            char ifname[IF_NAMESIZE] = {};
            unsigned mtu = 0;

            for (rtattr* rta = IFLA_RTA(msg); RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
                switch (rta->rta_type) {
                    case IFLA_IFNAME:
                        std::snprintf(ifname, sizeof(ifname), "%s", (char*)RTA_DATA(rta));
                        break;
                    case IFLA_MTU:
                        if (RTA_PAYLOAD(rta) >= sizeof(unsigned)) {
                            mtu = *(unsigned*)RTA_DATA(rta);
                        }
                        break;
                    default:
                        break;
                }
            }

            // Fall back to ifindex→name if attribute missing (rare)
            if (ifname[0] == '\0') {
                if_indextoname(msg->ifi_index, ifname);
            }

            std::string fl = flags_to_str(msg->ifi_flags);
            printf("%s: ifindex=%d flags=0x%x%s%s\n",
                   ifname[0] ? ifname : "(unknown)",
                   msg->ifi_index,
                   msg->ifi_flags,
                   fl.empty() ? "" : " [",
                   fl.empty() ? "" : (fl + "]").c_str());

            if (mtu) {
                printf("  mtu %u\n", mtu);
            }
        }
    }
}

int main(int argc, char** argv) {
    // interface eth0 up
    // interface list

    if (argc < 2) {
        std::cerr << "Usage: interface <name or command> <arguments>" << std::endl;
        std::cerr << "interface eth0 up" << std::endl;
        std::cerr << "interface list" << std::endl;
        return 0;
    }

    std::vector<std::string> m_argv(argc);
    NetlinkWrapper *netlinkWrapper = new NetlinkWrapper(ROUTE, UNICAST, true);

    for (int i=0; i<argc; i++) {
        m_argv[i] = std::string(argv[i]);
    }

    if (m_argv[1] == "list") {
        list_interfaces(netlinkWrapper);
    }
    else {
        set_interface_up(netlinkWrapper, m_argv[1], m_argv[2] == "up");
    }

    delete netlinkWrapper;

    return 0;
}
