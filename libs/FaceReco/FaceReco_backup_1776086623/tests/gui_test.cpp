#include <opencv2/opencv.hpp>
#include <iostream>

int main(){
    try{
        cv::Mat img = cv::Mat::zeros(480, 640, CV_8UC3);
        cv::putText(img, "GUI test", cv::Point(50,240), cv::FONT_HERSHEY_SIMPLEX, 2.0, cv::Scalar(0,255,0), 3);
        cv::namedWindow("gui_test_win", cv::WINDOW_AUTOSIZE);
        cv::imshow("gui_test_win", img);
        cv::waitKey(500);
        cv::imwrite("gui_test_screenshot.jpg", img);
        std::cout << "GUI test succeeded, screenshot saved to gui_test_screenshot.jpg" << std::endl;
        cv::destroyAllWindows();
        return 0;
    }catch(const cv::Exception &e){
        std::cerr << "GUI test failed: " << e.what() << std::endl;
        return -1;
    }
}
