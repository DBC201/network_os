#include <networking/DeviceHandler.h>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cout << "device_manager <abstract listener address> <abstract packet handler address>";
    }

    DeviceHandler deviceHandler(argv[1], argv[2]);

    deviceHandler.run();
    return 0;
}
