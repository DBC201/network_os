#ifndef MAC_UTILS_H
#define MAC_UTILS_H

#include <sstream>
#include <string>
#include <iomanip>
#include <string_utils.h>
#include <net/ethernet.h>

uint64_t pack_mac_str(std::string mac) {
    uint64_t result = 0;
    std::vector<std::string> byte_str = cpp_utils::string_utils::split(mac, ':');

    for (int i=0; i<byte_str.size(); i++) {
        // convert each hex byte to integer
        uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str[i], nullptr, 16));
        result = (result << 8) | byte;
    }

    return result;
}

std::string mac_to_str(const unsigned char* mac) {
    std::ostringstream oss;
    for (int i = 0; i < 6; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') 
            << static_cast<int>(mac[i]);
        if (i < 5) oss << ":";
    }
    return oss.str();
}

std::vector<unsigned char> mac_str_to_bytes(std::string mac_str) {
    std::vector<unsigned char> bytes;
    std::istringstream iss(mac_str);
    std::string token;

    while (std::getline(iss, token, ':')) {
        if (token.size() != 2) {
            throw std::invalid_argument("Invalid MAC segment: " + token);
        }
        unsigned int byte;
        std::stringstream ss;
        ss << std::hex << token;
        ss >> byte;
        bytes.push_back(static_cast<unsigned char>(byte));
    }

    if (bytes.size() != 6) {
        throw std::invalid_argument("Invalid MAC address length");
    }

    return bytes;
}

uint64_t pack_mac_bytes(const unsigned char* unpacked) {
    uint64_t packed = 0;

    for (int i = 0; i < 6; i++) {
        packed |= static_cast<uint64_t>(unpacked[i]) << (40 - 8*i);
    }

    return packed;
}

std::vector<unsigned char> unpack_mac_bytes(uint64_t packed) {
    std::vector<unsigned char> unpacked(6);

    for (int i = 0; i < 6; i++) {
        unpacked[i] = static_cast<unsigned char>((packed >> (40 - 8*i)) & 0xFF);
    }

    return unpacked;
}

uint64_t get_src_mac(unsigned char* frame) {
    ether_header* header = (ether_header *) frame;
    return pack_mac_bytes(header->ether_shost); 
}

#endif

