//
// Created by Xinghao Chen 2020/7/27
//
#include "doly/vision/vision_service.hpp"
#include "livefacereco.hpp"
#include <stdio.h>

#include <cstring>

namespace {

bool HasArgument(int argc, char** argv, const char* expected) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], expected) == 0) {
            return true;
        }
    }
    return false;
}

bool HasUnknownArguments(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "register_face") != 0) {
            std::cerr << "未知参数: " << argv[i] << std::endl;
            return true;
        }
    }
    return false;
}

}  // namespace

using namespace std;
using namespace cv;

int main(int argc, char** argv) {
    if (HasUnknownArguments(argc, argv)) {
        std::cerr << "用法: LiveFaceReco [register_face]" << std::endl;
        return 1;
    }

    SetInteractiveRegisterMode(HasArgument(argc, argv, "register_face"));

    doly::vision::VisionService service({
        "/home/pi/dolydev/libs/FaceReco/config.ini"
    });

    return service.run();
}
