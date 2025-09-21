#include <networking/PacketHandler.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: forwarder <abstract forwarder address>" << std::endl;
        return 1;
    }

    PacketHandler packetHandler;
    packetHandler.run(argv[1]);

    return 1;
}
