//
// Created by Xinghao Chen 2020/7/27
//
#include "doly/vision/vision_service.hpp"
#include <stdio.h>

using namespace std;
using namespace cv;

int main() {
    doly::vision::VisionService service({
        "/home/pi/dolydev/libs/FaceReco/config.ini"
    });

    return service.run();
}
