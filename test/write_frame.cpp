#include <linklayer/RawSocket.h>
#include <networking/linklayer/mac_utils.h>

using cpp_socket::linklayer::RawSocket;
using cpp_socket::linklayer::PROMISCIOUS;

int main(int argc, char** argv) {

    if (argc != 4) {
        std::cerr << "Usage: ./write_frame <ifname> <src mac> <destination mac>" << std::endl;
        return 0;
    }

    std::string ifname(argv[1]);
    std::string src_mac(argv[2]);
    std::string dest_mac(argv[3]);

    RawSocket rawSocket(ifname, PROMISCIOUS, true);

    std::vector<unsigned char> src_mac_bytes = mac_str_to_bytes(src_mac);
    std::vector<unsigned char> dest_mac_bytes = mac_str_to_bytes(dest_mac);
    
    uint16_t etherType = 0x0800;

    std::vector<unsigned char> frame;

    frame.insert(frame.end(), dest_mac_bytes.begin(), dest_mac_bytes.end());
    frame.insert(frame.end(), src_mac_bytes.begin(),  src_mac_bytes.end());
    frame.push_back(static_cast<unsigned char>((etherType >> 8) & 0xFF));
    frame.push_back(static_cast<unsigned char>( etherType & 0xFF));

    // Add dummy payload (at least 46 bytes)
    std::string payload = "Hello, this is a dummy packet!";
    frame.insert(frame.end(), payload.begin(), payload.end());

    // pad to 60 bytes total (header + payload)
    while (frame.size() < 60) {
        frame.push_back(0x00);
    }

    for (int i=0; i<1; i++) {
        int r = rawSocket.send_wrapper((const char*)frame.data(), frame.size(), 0);
        
        if (r < 0) {
            std::string error_message = "Error writing to " + ifname; 
            perror(error_message.data());
        }
    }

    return 0;
}
