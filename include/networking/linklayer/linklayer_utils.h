#ifndef LINKLAYER_UTILS_H
#define LINKLAYER_UTILS_H

#include <iomanip>
#include <iostream>

void print_mac(unsigned char* mac) {
	for (int i = 0; i < 6; i++) {
		std::cout << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(mac[i]);
		if (i < 5) std::cout << ":";
	}
	std::cout << std::dec << std::endl;
}

#endif
