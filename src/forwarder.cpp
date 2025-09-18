#include <networking/PacketHandler.h>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "forwarder <abstract listener address>";
    }

    PacketHandler packetHandler(argv[1]);

    return 0;
}
