#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include "../include/vision/ConfigManager.h"

namespace fs = std::filesystem;

/**
 * @class ConfigManagerTest
 * @brief ConfigManager 单元测试套件
 */
class ConfigManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config_mgr = CreateConfigManager();
    }

    void TearDown() override {
        config_mgr->Clear();
    }

    ConfigManagerPtr config_mgr;
};

/**
 * @test TestConfigManagerCreation
 * @brief 测试 ConfigManager 创建
 */
TEST_F(ConfigManagerTest, TestConfigManagerCreation) {
    ASSERT_NE(config_mgr, nullptr);
    EXPECT_EQ(config_mgr->GetSections().size(), 0);
}

/**
 * @test TestSetAndGetString
 * @brief 测试字符串的设置和获取
 */
TEST_F(ConfigManagerTest, TestSetAndGetString) {
    config_mgr->SetString("system", "name", "LiveFaceReco");
    
    EXPECT_EQ(config_mgr->GetString("system", "name"), "LiveFaceReco");
    EXPECT_TRUE(config_mgr->HasKey("system", "name"));
    EXPECT_TRUE(config_mgr->HasSection("system"));
}

/**
 * @test TestSetAndGetInt
 * @brief 测试整数的设置和获取
 */
TEST_F(ConfigManagerTest, TestSetAndGetInt) {
    config_mgr->SetInt("video", "width", 640);
    config_mgr->SetInt("video", "height", 480);
    
    EXPECT_EQ(config_mgr->GetInt("video", "width"), 640);
    EXPECT_EQ(config_mgr->GetInt("video", "height"), 480);
}

/**
 * @test TestSetAndGetFloat
 * @brief 测试浮点数的设置和获取
 */
TEST_F(ConfigManagerTest, TestSetAndGetFloat) {
    config_mgr->SetFloat("detection", "threshold", 0.75f);
    
    float val = config_mgr->GetFloat("detection", "threshold");
    EXPECT_NEAR(val, 0.75f, 0.001f);
}

/**
 * @test TestSetAndGetBool
 * @brief 测试布尔值的设置和获取
 */
TEST_F(ConfigManagerTest, TestSetAndGetBool) {
    config_mgr->SetBool("features", "enable_face_detection", true);
    config_mgr->SetBool("features", "enable_gesture", false);
    
    EXPECT_TRUE(config_mgr->GetBool("features", "enable_face_detection"));
    EXPECT_FALSE(config_mgr->GetBool("features", "enable_gesture"));
}

/**
 * @test TestDefaultValues
 * @brief 测试获取不存在的键时返回默认值
 */
TEST_F(ConfigManagerTest, TestDefaultValues) {
    EXPECT_EQ(config_mgr->GetString("missing", "key", "default"), "default");
    EXPECT_EQ(config_mgr->GetInt("missing", "key", -1), -1);
    EXPECT_FLOAT_EQ(config_mgr->GetFloat("missing", "key", 0.5f), 0.5f);
    EXPECT_FALSE(config_mgr->GetBool("missing", "key", false));
}

/**
 * @test TestHasKeyAndHasSection
 * @brief 测试键和节的检查
 */
TEST_F(ConfigManagerTest, TestHasKeyAndHasSection) {
    config_mgr->SetString("test", "key1", "value1");
    
    EXPECT_TRUE(config_mgr->HasSection("test"));
    EXPECT_TRUE(config_mgr->HasKey("test", "key1"));
    EXPECT_FALSE(config_mgr->HasKey("test", "key2"));
    EXPECT_FALSE(config_mgr->HasSection("missing"));
}

/**
 * @test TestGetKeys
 * @brief 测试获取节下的所有键
 */
TEST_F(ConfigManagerTest, TestGetKeys) {
    config_mgr->SetString("section1", "key1", "val1");
    config_mgr->SetString("section1", "key2", "val2");
    config_mgr->SetString("section1", "key3", "val3");
    
    auto keys = config_mgr->GetKeys("section1");
    EXPECT_EQ(keys.size(), 3);
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "key1") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "key2") != keys.end());
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "key3") != keys.end());
}

/**
 * @test TestGetSections
 * @brief 测试获取所有节
 */
TEST_F(ConfigManagerTest, TestGetSections) {
    config_mgr->SetString("section1", "key", "val");
    config_mgr->SetString("section2", "key", "val");
    config_mgr->SetString("section3", "key", "val");
    
    auto sections = config_mgr->GetSections();
    EXPECT_EQ(sections.size(), 3);
    EXPECT_TRUE(std::find(sections.begin(), sections.end(), "section1") != sections.end());
    EXPECT_TRUE(std::find(sections.begin(), sections.end(), "section2") != sections.end());
}

/**
 * @test TestLoadFromINI
 * @brief 测试从 INI 文件加载配置
 */
TEST_F(ConfigManagerTest, TestLoadFromINI) {
    // 获取当前工作目录并构建配置文件路径
    std::string ini_path = "/home/pi/deps/LiveFaceReco_RaspberryPi/tests/config_test.ini";
    bool success = config_mgr->LoadFromINI(ini_path);
    ASSERT_TRUE(success) << "Failed to load INI file from: " << ini_path;
    
    // 验证 [system] 节
    EXPECT_EQ(config_mgr->GetInt("system", "resolution_width"), 640);
    EXPECT_EQ(config_mgr->GetInt("system", "resolution_height"), 480);
    EXPECT_EQ(config_mgr->GetInt("system", "frame_rate"), 15);
    EXPECT_EQ(config_mgr->GetString("system", "input_source"), "camera");
    
    // 验证 [modules] 节
    EXPECT_TRUE(config_mgr->GetBool("modules", "enable_face_detection"));
    EXPECT_TRUE(config_mgr->GetBool("modules", "enable_motion_detection"));
    EXPECT_NEAR(config_mgr->GetFloat("modules", "face_detection_threshold"), 0.5f, 0.001f);
    
    // 验证 [performance] 节
    EXPECT_EQ(config_mgr->GetInt("performance", "max_threads"), 4);
    EXPECT_EQ(config_mgr->GetInt("performance", "max_memory_mb"), 500);
}

/**
 * @test TestLoadFromYAML
 * @brief 测试从 YAML 文件加载配置
 */
TEST_F(ConfigManagerTest, TestLoadFromYAML) {
    std::string yaml_path = "/home/pi/deps/LiveFaceReco_RaspberryPi/tests/config_test.yaml";
    bool success = config_mgr->LoadFromYAML(yaml_path);
    ASSERT_TRUE(success) << "Failed to load YAML file from: " << yaml_path;
    
    // 验证 system 节
    EXPECT_EQ(config_mgr->GetInt("system", "resolution_width"), 1280);
    EXPECT_EQ(config_mgr->GetInt("system", "resolution_height"), 720);
    EXPECT_EQ(config_mgr->GetString("system", "input_source"), "rtsp_stream");
    
    // 验证 modules 节
    EXPECT_TRUE(config_mgr->GetBool("modules", "enable_face_detection"));
    EXPECT_FALSE(config_mgr->GetBool("modules", "enable_gesture_recognition"));
    
    // 验证 performance 节
    EXPECT_EQ(config_mgr->GetInt("performance", "max_threads"), 8);
    EXPECT_EQ(config_mgr->GetInt("performance", "max_memory_mb"), 800);
}

/**
 * @test TestSaveAndLoadINI
 * @brief 测试保存和重新加载 INI 配置
 */
TEST_F(ConfigManagerTest, TestSaveAndLoadINI) {
    // 设置配置
    config_mgr->SetString("test_section", "string_key", "test_value");
    config_mgr->SetInt("test_section", "int_key", 123);
    config_mgr->SetFloat("test_section", "float_key", 3.14f);
    config_mgr->SetBool("test_section", "bool_key", true);
    
    // 保存到临时文件
    std::string temp_ini = "/tmp/test_config.ini";
    bool save_success = config_mgr->SaveToINI(temp_ini);
    ASSERT_TRUE(save_success);
    
    // 清空配置
    config_mgr->Clear();
    EXPECT_EQ(config_mgr->GetSections().size(), 0);
    
    // 重新加载
    bool load_success = config_mgr->LoadFromINI(temp_ini);
    ASSERT_TRUE(load_success);
    
    // 验证
    EXPECT_EQ(config_mgr->GetString("test_section", "string_key"), "test_value");
    EXPECT_EQ(config_mgr->GetInt("test_section", "int_key"), 123);
    EXPECT_NEAR(config_mgr->GetFloat("test_section", "float_key"), 3.14f, 0.01f);
    EXPECT_TRUE(config_mgr->GetBool("test_section", "bool_key"));
    
    // 清理临时文件
    fs::remove(temp_ini);
}

/**
 * @test TestTypeConversions
 * @brief 测试类型转换（字符串读取的类型转换）
 */
TEST_F(ConfigManagerTest, TestTypeConversions) {
    // 存储为字符串，读取为其他类型
    config_mgr->SetString("convert", "int_str", "42");
    config_mgr->SetString("convert", "float_str", "3.14");
    config_mgr->SetString("convert", "bool_str_true", "true");
    config_mgr->SetString("convert", "bool_str_false", "false");
    
    // 类型转换
    EXPECT_EQ(config_mgr->GetInt("convert", "int_str"), 42);
    EXPECT_NEAR(config_mgr->GetFloat("convert", "float_str"), 3.14f, 0.01f);
    EXPECT_TRUE(config_mgr->GetBool("convert", "bool_str_true"));
    EXPECT_FALSE(config_mgr->GetBool("convert", "bool_str_false"));
}

/**
 * @test TestBooleanVariants
 * @brief 测试布尔值的多种表示
 */
TEST_F(ConfigManagerTest, TestBooleanVariants) {
    config_mgr->SetString("bool_test", "true_variants", "true");
    config_mgr->SetString("bool_test", "yes_variant", "yes");
    config_mgr->SetString("bool_test", "one_variant", "1");
    config_mgr->SetString("bool_test", "on_variant", "on");
    
    config_mgr->SetString("bool_test", "false_variant", "false");
    config_mgr->SetString("bool_test", "no_variant", "no");
    config_mgr->SetString("bool_test", "zero_variant", "0");
    config_mgr->SetString("bool_test", "off_variant", "off");
    
    // 验证真值
    EXPECT_TRUE(config_mgr->GetBool("bool_test", "true_variants"));
    EXPECT_TRUE(config_mgr->GetBool("bool_test", "yes_variant"));
    EXPECT_TRUE(config_mgr->GetBool("bool_test", "one_variant"));
    EXPECT_TRUE(config_mgr->GetBool("bool_test", "on_variant"));
    
    // 验证假值
    EXPECT_FALSE(config_mgr->GetBool("bool_test", "false_variant"));
    EXPECT_FALSE(config_mgr->GetBool("bool_test", "no_variant"));
    EXPECT_FALSE(config_mgr->GetBool("bool_test", "zero_variant"));
    EXPECT_FALSE(config_mgr->GetBool("bool_test", "off_variant"));
}

/**
 * @test TestGetSummary
 * @brief 测试获取配置摘要
 */
TEST_F(ConfigManagerTest, TestGetSummary) {
    config_mgr->SetString("section1", "key1", "value1");
    config_mgr->SetString("section2", "key2", "value2");
    
    std::string summary = config_mgr->GetSummary();
    EXPECT_TRUE(summary.find("section1") != std::string::npos);
    EXPECT_TRUE(summary.find("key1") != std::string::npos);
    EXPECT_TRUE(summary.find("value1") != std::string::npos);
}

/**
 * @test TestClear
 * @brief 测试清空配置
 */
TEST_F(ConfigManagerTest, TestClear) {
    config_mgr->SetString("section1", "key1", "value1");
    config_mgr->SetString("section2", "key2", "value2");
    EXPECT_EQ(config_mgr->GetSections().size(), 2);
    
    config_mgr->Clear();
    EXPECT_EQ(config_mgr->GetSections().size(), 0);
    EXPECT_FALSE(config_mgr->HasSection("section1"));
}

/**
 * @test TestINIWithComments
 * @brief 测试包含注释的 INI 文件
 */
TEST_F(ConfigManagerTest, TestINIWithComments) {
    // 创建包含注释的临时 INI 文件
    std::string temp_ini = "/tmp/test_comments.ini";
    std::ofstream file(temp_ini);
    file << "; This is a comment\n";
    file << "[section1]\n";
    file << "# Another comment\n";
    file << "key1 = value1\n";
    file << "; Inline comment style\n";
    file << "key2 = value2\n";
    file.close();
    
    // 加载配置
    bool success = config_mgr->LoadFromINI(temp_ini);
    ASSERT_TRUE(success);
    
    // 验证注释被正确忽略
    EXPECT_EQ(config_mgr->GetString("section1", "key1"), "value1");
    EXPECT_EQ(config_mgr->GetString("section1", "key2"), "value2");
    
    // 清理
    fs::remove(temp_ini);
}

/**
 * @test TestMultipleSections
 * @brief 测试多个节的配置
 */
TEST_F(ConfigManagerTest, TestMultipleSections) {
    // 设置多个节
    for (int i = 0; i < 5; ++i) {
        std::string section = "section_" + std::to_string(i);
        for (int j = 0; j < 3; ++j) {
            std::string key = "key_" + std::to_string(j);
            config_mgr->SetString(section, key, "value");
        }
    }
    
    // 验证
    EXPECT_EQ(config_mgr->GetSections().size(), 5);
    for (int i = 0; i < 5; ++i) {
        std::string section = "section_" + std::to_string(i);
        auto keys = config_mgr->GetKeys(section);
        EXPECT_EQ(keys.size(), 3);
    }
}

/**
 * @test TestInvalidINIFile
 * @brief 测试无效的 INI 文件
 */
TEST_F(ConfigManagerTest, TestInvalidINIFile) {
    // 尝试加载不存在的文件
    bool success = config_mgr->LoadFromINI("/nonexistent/file.ini");
    EXPECT_FALSE(success);
}

/**
 * @test TestConfigManagerSmartPointer
 * @brief 测试 ConfigManager 智能指针
 */
TEST_F(ConfigManagerTest, TestConfigManagerSmartPointer) {
    {
        auto cfg = CreateConfigManager();
        cfg->SetString("test", "key", "value");
        EXPECT_EQ(cfg->GetString("test", "key"), "value");
    }
    // 智能指针自动释放
}
