#include <networking/PacketHandler.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "forwarder <abstract forwarder address>" << std::endl;
        return 0;
    }

    PacketHandler packetHandler;
    packetHandler.run(argv[1]);

    return 0;
}
