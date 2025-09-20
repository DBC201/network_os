#include <networking/DeviceHandler.h>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "device_manager <abstract device_manager address> <abstract forwarder address>" << std::endl;
        return 0;
    }

    DeviceHandler deviceHandler(argv[1], argv[2]);

    deviceHandler.run();
    return 0;
}
