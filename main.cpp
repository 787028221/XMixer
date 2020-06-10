//
// Created by Andy on 2020/5/22.
//

#include "XDecoder.h"
#include "XMixer.h"
#include <iostream>

std::string inFilename = "/Users/andy/Desktop/Mixer/assets/jieqian.mp3";
std::string outPath = "/Users/andy/Desktop/output.aac";

void testMixer() {
    auto mixer = std::make_unique<XMixer>();
    try {
        mixer->add(inFilename);
        mixer->mix(outPath);
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
}


int main(int argc, char *argv[]) {
    testMixer();
    return 0;
}
