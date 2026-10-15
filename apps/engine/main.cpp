// SPDX-License-Identifier: GPL-3.0-or-later
#include <mapgyver/build_info.hpp>
#include <iostream>
int main() {
    const auto info = mapgyver::build_info();
    std::cout << info.product << ' ' << info.version << '\n';
}
