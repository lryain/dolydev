#include <gtest/gtest.h>
#include <opencv2/core.hpp>
#include <vector>
#include "../include/vision/FrameContext.h"

/**
 * @class FrameContextTest
 * @brief FrameContext 单元测试套件
 */
class FrameContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建测试用的简单图像
        test_image = cv::Mat(480, 640, CV_8UC3, cv::Scalar(100, 150, 200));
    }

    cv::Mat test_image;
};

/**
 * @test TestFrameContextCreation
 * @brief 测试 FrameContext 创建和初始化
 */
TEST_F(FrameContextTest, TestFrameContextCreation) {
    auto ctx = CreateFrameContext();
    ASSERT_NE(ctx, nullptr);
    
    // 验证初始状态
    EXPECT_TRUE(ctx->raw_frame.empty());
    EXPECT_TRUE(ctx->display_frame.empty());
    EXPECT_EQ(ctx->metadata.frame_id, 0);
    EXPECT_EQ(ctx->metadata.timestamp_us, 0);
    EXPECT_EQ(ctx->data_bus.size(), 0);
}

/**
 * @test TestFrameDataAssignment
 * @brief 测试帧数据赋值
 */
TEST_F(FrameContextTest, TestFrameDataAssignment) {
    auto ctx = CreateFrameContext();
    
    // 赋值原始帧
    ctx->raw_frame = test_image.clone();
    EXPECT_FALSE(ctx->raw_frame.empty());
    EXPECT_EQ(ctx->raw_frame.rows, 480);
    EXPECT_EQ(ctx->raw_frame.cols, 640);
    EXPECT_EQ(ctx->raw_frame.channels(), 3);
    
    // 赋值显示帧
    ctx->display_frame = test_image.clone();
    EXPECT_FALSE(ctx->display_frame.empty());
    
    // 验证帧是否有效
    EXPECT_TRUE(ctx->IsValid());
}

/**
 * @test TestFrameMetadata
 * @brief 测试帧元数据管理
 */
TEST_F(FrameContextTest, TestFrameMetadata) {
    auto ctx = CreateFrameContext();
    
    // 设置元数据
    ctx->metadata.frame_id = 100;
    ctx->metadata.timestamp_us = 1000000;
    ctx->metadata.brightness = 0.75f;
    ctx->metadata.motion_level = 0.5f;
    ctx->metadata.source = "camera_0";
    ctx->metadata.processing_time_ms = 50;
    
    // 验证元数据
    EXPECT_EQ(ctx->metadata.frame_id, 100);
    EXPECT_EQ(ctx->metadata.timestamp_us, 1000000);
    EXPECT_FLOAT_EQ(ctx->metadata.brightness, 0.75f);
    EXPECT_FLOAT_EQ(ctx->metadata.motion_level, 0.5f);
    EXPECT_EQ(ctx->metadata.source, "camera_0");
    EXPECT_EQ(ctx->metadata.processing_time_ms, 50);
}

/**
 * @test TestDataBusSetGet
 * @brief 测试 data_bus 基本的 set/get 操作
 */
TEST_F(FrameContextTest, TestDataBusSetGet) {
    auto ctx = CreateFrameContext();
    
    // 测试 bool 类型
    ctx->SetData<bool>("motion_detected", true);
    EXPECT_TRUE(ctx->GetData<bool>("motion_detected"));
    
    // 测试 int 类型
    ctx->SetData<int>("face_count", 3);
    EXPECT_EQ(ctx->GetData<int>("face_count"), 3);
    
    // 测试 float 类型
    ctx->SetData<float>("confidence", 0.95f);
    EXPECT_FLOAT_EQ(ctx->GetData<float>("confidence"), 0.95f);
    
    // 测试 string 类型
    ctx->SetData<std::string>("person_name", "Alice");
    EXPECT_EQ(ctx->GetData<std::string>("person_name"), "Alice");
}

/**
 * @test TestDataBusMatStorage
 * @brief 测试 data_bus 存储 cv::Mat 对象
 */
TEST_F(FrameContextTest, TestDataBusMatStorage) {
    auto ctx = CreateFrameContext();
    
    // 创建测试 Mat
    cv::Mat mask = cv::Mat(480, 640, CV_8U, cv::Scalar(0));
    mask.at<uchar>(100, 100) = 255;
    
    // 存储到 data_bus
    ctx->SetData<cv::Mat>("motion_mask", mask);
    
    // 读取验证
    cv::Mat retrieved_mask = ctx->GetData<cv::Mat>("motion_mask");
    EXPECT_FALSE(retrieved_mask.empty());
    EXPECT_EQ(retrieved_mask.rows, 480);
    EXPECT_EQ(retrieved_mask.cols, 640);
    EXPECT_EQ(retrieved_mask.at<uchar>(100, 100), 255);
}

/**
 * @test TestDataBusVectorStorage
 * @brief 测试 data_bus 存储 vector 对象
 */
TEST_F(FrameContextTest, TestDataBusVectorStorage) {
    auto ctx = CreateFrameContext();
    
    // 创建测试 vector
    std::vector<cv::Rect> faces = {
        cv::Rect(10, 20, 100, 120),
        cv::Rect(150, 30, 95, 110)
    };
    ctx->SetData<std::vector<cv::Rect>>("face_detections", faces);
    
    // 读取验证
    auto retrieved_faces = ctx->GetData<std::vector<cv::Rect>>("face_detections");
    EXPECT_EQ(retrieved_faces.size(), 2);
    EXPECT_EQ(retrieved_faces[0].x, 10);
    EXPECT_EQ(retrieved_faces[0].y, 20);
    EXPECT_EQ(retrieved_faces[1].x, 150);
}

/**
 * @test TestDataBusDefaultValue
 * @brief 测试 data_bus 获取不存在的键时返回默认值
 */
TEST_F(FrameContextTest, TestDataBusDefaultValue) {
    auto ctx = CreateFrameContext();
    
    // 获取不存在的键（使用默认值）
    bool default_bool = ctx->GetData<bool>("nonexistent", false);
    EXPECT_FALSE(default_bool);
    
    int default_int = ctx->GetData<int>("missing_int", -1);
    EXPECT_EQ(default_int, -1);
    
    std::string default_str = ctx->GetData<std::string>("missing_str", "default");
    EXPECT_EQ(default_str, "default");
}

/**
 * @test TestDataBusHasData
 * @brief 测试 data_bus 的键检查函数
 */
TEST_F(FrameContextTest, TestDataBusHasData) {
    auto ctx = CreateFrameContext();
    
    ctx->SetData<int>("count", 5);
    
    EXPECT_TRUE(ctx->HasData("count"));
    EXPECT_FALSE(ctx->HasData("nonexistent"));
}

/**
 * @test TestDataBusClearData
 * @brief 测试 data_bus 的单键清除和全部清除
 */
TEST_F(FrameContextTest, TestDataBusClearData) {
    auto ctx = CreateFrameContext();
    
    ctx->SetData<int>("key1", 10);
    ctx->SetData<int>("key2", 20);
    ctx->SetData<int>("key3", 30);
    
    EXPECT_EQ(ctx->data_bus.size(), 3);
    
    // 清除单个键
    ctx->ClearData("key2");
    EXPECT_EQ(ctx->data_bus.size(), 2);
    EXPECT_FALSE(ctx->HasData("key2"));
    EXPECT_TRUE(ctx->HasData("key1"));
    
    // 清除所有数据
    ctx->ClearAllData();
    EXPECT_EQ(ctx->data_bus.size(), 0);
    EXPECT_FALSE(ctx->HasData("key1"));
}

/**
 * @test TestDataBusTypeMismatch
 * @brief 测试 data_bus 类型不匹配的处理
 */
TEST_F(FrameContextTest, TestDataBusTypeMismatch) {
    auto ctx = CreateFrameContext();
    
    // 存储 int，尝试读取为 string（应返回默认值）
    ctx->SetData<int>("value", 42);
    std::string retrieved = ctx->GetData<std::string>("value", "default");
    EXPECT_EQ(retrieved, "default");
    
    // 存储 string，尝试读取为 float（应返回默认值）
    ctx->SetData<std::string>("text", "hello");
    float result = ctx->GetData<float>("text", 0.0f);
    EXPECT_FLOAT_EQ(result, 0.0f);
}

/**
 * @test TestGetFrameSize
 * @brief 测试获取帧分辨率
 */
TEST_F(FrameContextTest, TestGetFrameSize) {
    auto ctx = CreateFrameContext();
    
    // 未赋值时
    EXPECT_EQ(ctx->GetFrameSize(), cv::Size(0, 0));
    
    // 赋值后
    ctx->raw_frame = test_image.clone();
    cv::Size size = ctx->GetFrameSize();
    EXPECT_EQ(size.width, 640);
    EXPECT_EQ(size.height, 480);
}

/**
 * @test TestDataBusMultipleModulesSimulation
 * @brief 模拟多个模块通过 data_bus 进行通信的场景
 */
TEST_F(FrameContextTest, TestDataBusMultipleModulesSimulation) {
    auto ctx = CreateFrameContext();
    ctx->raw_frame = test_image.clone();
    
    // 模块1：LightDetection - 检测亮度
    ctx->SetData<float>("brightness", 0.6f);
    ctx->SetData<bool>("low_light", false);
    
    // 模块2：MotionDetection - 检测运动
    cv::Mat motion_mask = cv::Mat(480, 640, CV_8U, cv::Scalar(0));
    ctx->SetData<cv::Mat>("motion_mask", motion_mask);
    ctx->SetData<float>("motion_level", 0.3f);
    
    // 模块3：FaceDetection - 检测人脸
    std::vector<cv::Rect> faces = {cv::Rect(100, 100, 100, 120)};
    ctx->SetData<std::vector<cv::Rect>>("face_detections", faces);
    
    // 验证所有数据
    EXPECT_FLOAT_EQ(ctx->GetData<float>("brightness"), 0.6f);
    EXPECT_FALSE(ctx->GetData<bool>("low_light"));
    EXPECT_FALSE(ctx->GetData<cv::Mat>("motion_mask").empty());
    EXPECT_FLOAT_EQ(ctx->GetData<float>("motion_level"), 0.3f);
    EXPECT_EQ(ctx->GetData<std::vector<cv::Rect>>("face_detections").size(), 1);
}

/**
 * @test TestFrameContextSmartPointer
 * @brief 测试 FrameContext 智能指针的生命周期管理
 */
TEST_F(FrameContextTest, TestFrameContextSmartPointer) {
    // 在作用域内创建智能指针
    {
        auto ctx = CreateFrameContext();
        ctx->raw_frame = test_image.clone();
        ctx->SetData<int>("count", 100);
        EXPECT_FALSE(ctx->raw_frame.empty());
    }
    // 作用域结束，智能指针自动释放，不需要显式 delete
    
    // 验证智能指针可以安全地被复制
    auto ctx1 = CreateFrameContext();
    auto ctx2 = ctx1;
    EXPECT_EQ(ctx1.get(), ctx2.get());  // 指向同一对象
}

// ==================== 性能测试 ====================

/**
 * @test TestDataBusPerformance
 * @brief 性能测试：大量 data_bus 操作
 */
TEST_F(FrameContextTest, TestDataBusPerformance) {
    auto ctx = CreateFrameContext();
    
    // 插入 1000 个键值对
    for (int i = 0; i < 1000; ++i) {
        ctx->SetData<int>("key_" + std::to_string(i), i);
    }
    
    // 验证数据量
    EXPECT_EQ(ctx->data_bus.size(), 1000);
    
    // 随机访问
    EXPECT_EQ(ctx->GetData<int>("key_500"), 500);
    EXPECT_EQ(ctx->GetData<int>("key_999"), 999);
    
    // 清除
    ctx->ClearAllData();
    EXPECT_EQ(ctx->data_bus.size(), 0);
}

/**
 * @test TestLargeMatrixStorage
 * @brief 性能测试：存储大型 Mat 对象
 */
TEST_F(FrameContextTest, TestLargeMatrixStorage) {
    auto ctx = CreateFrameContext();
    
    // 创建大型矩阵 (1920x1080, float32)
    cv::Mat large_mat = cv::Mat(1080, 1920, CV_32F, cv::Scalar(0.5f));
    ctx->SetData<cv::Mat>("large_data", large_mat);
    
    // 验证
    auto retrieved = ctx->GetData<cv::Mat>("large_data");
    EXPECT_EQ(retrieved.rows, 1080);
    EXPECT_EQ(retrieved.cols, 1920);
    EXPECT_EQ(retrieved.type(), CV_32F);
}
