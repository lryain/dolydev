//
// Created by Xinghao Chen 2020/7/27
//
#include <iostream>
#include <stdio.h>

#include "stdlib.h"
#include <iostream>
#include <array>
#include <vector>
#include <opencv2/opencv.hpp>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include "arcface.h"
#include "live.h"
#include "doly/vision/runtime_control.hpp"
#include "doly/vision/vision_bus_bridge.hpp"
using namespace std;
using namespace cv;

// Adjustable Parameters (moved to runtime-configurable variables)
extern bool largest_face_only;
extern bool record_face;
extern int distance_threshold;
extern float face_thre;
extern float true_thre;
extern int jump;
extern int input_width;
extern int input_height;
extern int output_width;
extern int output_height;
extern float angle_threshold;
extern std::string project_path;
// end

extern cv::Size frame_size;
extern float ratio_x;
extern float ratio_y;

void loadLiveModel(Live & live );
int MTCNNDetection(doly::vision::RuntimeControl& control,
                   doly::vision::VisionBusBridge& bus,
                   doly::vision::RuntimeMetrics& metrics);

void SetVisionRuntimeContext(doly::vision::RuntimeControl* control,
                             doly::vision::VisionBusBridge* bus,
                             doly::vision::RuntimeMetrics* metrics);

// 🆕 视频发布器集成
#include <nora/coms/video_stream/video_stream_publisher.h>
#include <memory>

extern std::unique_ptr<nora::coms::video_stream::VideoStreamPublisher> g_video_publisher;

bool InitializeVideoPublisher();
void PublishCurrentFrame(const cv::Mat& frame);
void ShutdownVideoPublisher();


