//
// Created by markson zhang
//
//
// Edited by Xinghao Chen 2020/7/27
//
//
// Refactored and edited by Luiz Correia 2021/06/20


#include <math.h>
#include "livefacereco.hpp"
#include <time.h>
#include "math.hpp"
#include "lccv.hpp"
#include "mtcnn_new.h"
#include "FacePreprocess.h"
#include "DatasetHandler/image_dataset_handler.hpp"
#include <algorithm>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <cstring>
#include <unistd.h>
#include <iomanip>
#include <sstream>
#include <queue>
#include <map>
#include <set>
#include <filesystem>
#include <zmq.hpp>

#include <nlohmann/json.hpp>
#include "doly/vision/yaml_config.hpp"
#include "doly/vision/face_database.hpp"

#define PI 3.14159265
using namespace std;

double sum_score, sum_fps,sum_confidence;

using json = nlohmann::json;
using doly::vision::YAMLConfig;

#define PROJECT_PATH "/home/pi/dolydev/libs/FaceReco"

#include "config.hpp"
#include "vision/ConfigManager.h"

static doly::vision::RuntimeControl* g_runtime_control = nullptr;
static doly::vision::VisionBusBridge* g_bus_bridge = nullptr;
static doly::vision::RuntimeMetrics* g_runtime_metrics = nullptr;
static bool g_interactive_register_mode = false;

void SetVisionRuntimeContext(doly::vision::RuntimeControl* control,
                             doly::vision::VisionBusBridge* bus,
                             doly::vision::RuntimeMetrics* metrics) {
    g_runtime_control = control;
    g_bus_bridge = bus;
    g_runtime_metrics = metrics;
}

void SetInteractiveRegisterMode(bool enabled) {
    g_interactive_register_mode = enabled;
}

bool IsInteractiveRegisterMode() {
    return g_interactive_register_mode;
}

#include <fstream>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <queue>

#include <map>

// ============ VIDEO PUBLISHER INTEGRATION ============
// 🆕 视频发布器全局对象
std::unique_ptr<nora::coms::video_stream::VideoStreamPublisher> g_video_publisher = nullptr;

static int publish_frame_count = 0;
static int publish_error_count = 0;
static auto publish_start_time = std::chrono::high_resolution_clock::now();
static bool first_frame_published = false;

// 🆕 视频流配置参数（从config.ini读取）
static bool g_enable_video_stream_to_eyeengine = true;  // 默认启用
static int g_video_stream_target_lcd = 1;                // 默认发送到右眼(1)

namespace {

constexpr const char* kEyeEngineCommandEndpoint = "ipc:///tmp/doly_eye_cmd.sock";
constexpr int kEyeEngineCommandTimeoutMs = 300;
constexpr int kEyeEngineEnableRetryMs = 2000;

bool g_eyeengine_stream_enabled = false;
auto g_eyeengine_next_enable_retry = std::chrono::steady_clock::time_point::min();

bool SendEyeEngineCommand(const json& command, json* response = nullptr) {
    try {
        zmq::context_t context(1);
        zmq::socket_t socket(context, zmq::socket_type::req);

        int timeout_ms = kEyeEngineCommandTimeoutMs;
        int linger_ms = 0;
        socket.set(zmq::sockopt::rcvtimeo, timeout_ms);
        socket.set(zmq::sockopt::sndtimeo, timeout_ms);
        socket.set(zmq::sockopt::linger, linger_ms);
        socket.connect(kEyeEngineCommandEndpoint);

        const std::string payload = command.dump();
        const auto send_result = socket.send(zmq::buffer(payload), zmq::send_flags::none);
        if (!send_result) {
            std::cerr << "[FaceReco] 向 eyeEngine 发送命令失败: " << payload << std::endl;
            return false;
        }

        zmq::message_t reply;
        const auto recv_result = socket.recv(reply, zmq::recv_flags::none);
        if (!recv_result) {
            std::cerr << "[FaceReco] eyeEngine 命令超时: " << payload << std::endl;
            return false;
        }

        const std::string reply_text(static_cast<const char*>(reply.data()), reply.size());
        json parsed = json::parse(reply_text, nullptr, false);
        if (parsed.is_discarded()) {
            std::cerr << "[FaceReco] eyeEngine 返回了不可解析响应: " << reply_text << std::endl;
            return false;
        }

        if (response) {
            *response = parsed;
        }

        if (!parsed.value("success", false)) {
            std::cerr << "[FaceReco] eyeEngine 命令失败: " << parsed.dump() << std::endl;
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "[FaceReco] eyeEngine 命令异常: " << e.what() << std::endl;
        return false;
    }
}

bool EnsureEyeEngineVideoStreamEnabled(bool force_retry = false) {
    if (!g_enable_video_stream_to_eyeengine) {
        return true;
    }

    if (g_eyeengine_stream_enabled && !force_retry) {
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!force_retry && now < g_eyeengine_next_enable_retry) {
        return false;
    }

    json response;
    const bool ok = SendEyeEngineCommand(
        {
            {"action", "enable_video_stream"},
            {"target_lcd", g_video_stream_target_lcd},
            {"fps", std::max(Settings::getInt("cap_fps", 15), 1)}
        },
        &response);

    if (ok) {
        g_eyeengine_stream_enabled = true;
        g_eyeengine_next_enable_retry = std::chrono::steady_clock::time_point::min();
        std::cout << "[FaceReco] 已通知 eyeEngine 启用视频流, target_lcd="
                  << g_video_stream_target_lcd << std::endl;
        return true;
    }

    g_eyeengine_stream_enabled = false;
    g_eyeengine_next_enable_retry = now + std::chrono::milliseconds(kEyeEngineEnableRetryMs);
    return false;
}

void DisableEyeEngineVideoStream() {
    if (!g_eyeengine_stream_enabled) {
        g_eyeengine_next_enable_retry = std::chrono::steady_clock::time_point::min();
        return;
    }

    json response;
    if (!SendEyeEngineCommand({{"action", "disable_video_stream"}}, &response)) {
        std::cerr << "[FaceReco] eyeEngine 视频流关闭命令发送失败" << std::endl;
    }

    g_eyeengine_stream_enabled = false;
    g_eyeengine_next_enable_retry = std::chrono::steady_clock::time_point::min();
}

}  // namespace

// 🆕 初始化视频发布器
bool InitializeVideoPublisher() {
    try {
        // 🆕 从配置文件读取视频流启用参数
        g_enable_video_stream_to_eyeengine = Settings::getBool("enable_video_stream_to_eyeengine", true);
        g_video_stream_target_lcd = Settings::getInt("video_stream_target_lcd", 1);
        
        std::cout << "[DEBUG] 视频流配置: enable_video_stream_to_eyeengine=" 
                  << (g_enable_video_stream_to_eyeengine ? "true" : "false")
                  << ", target_lcd=" << g_video_stream_target_lcd << std::endl;
        
        // 如果视频流禁用，则无需初始化发布器
        if (!g_enable_video_stream_to_eyeengine) {
            std::cout << "⏭️  视频发布器已被配置禁用 (enable_video_stream_to_eyeengine=false)" << std::endl;
            g_video_publisher.reset();
            g_eyeengine_stream_enabled = false;
            g_eyeengine_next_enable_retry = std::chrono::steady_clock::time_point::min();
            return true;  // 返回 true 表示初始化成功（虽然未创建发布器）
        }
        
        std::cout << "[DEBUG] 开始初始化视频发布器..." << std::endl;
        
        // 创建配置
        nora::coms::video_stream::VideoPublisherConfig config;
        config.resource_id = "facereco_video";
        config.instance_id = 0;
        config.default_timeout_ms = 5000;
        config.enable_statistics = true;
        
        std::cout << "[DEBUG] 配置参数: resource_id=" << config.resource_id 
                  << ", instance_id=" << (int)config.instance_id 
                  << ", timeout_ms=" << config.default_timeout_ms << std::endl;
        
        g_video_publisher = std::make_unique<nora::coms::video_stream::VideoStreamPublisher>(config);
        std::cout << "[DEBUG] VideoStreamPublisher 对象创建成功" << std::endl;
        
        if (!g_video_publisher->Initialize()) {
            std::cerr << "❌ 视频发布器初始化失败 - Initialize() 返回 false" << std::endl;
            g_video_publisher.reset();
            return false;
        }
        std::cout << "✅ 视频发布器初始化成功 (resource_id=facereco_video, target_lcd=" << g_video_stream_target_lcd << ")" << std::endl;
        publish_frame_count = 0;
        publish_error_count = 0;
        publish_start_time = std::chrono::high_resolution_clock::now();
        first_frame_published = false;
        g_eyeengine_stream_enabled = false;
        g_eyeengine_next_enable_retry = std::chrono::steady_clock::time_point::min();
        EnsureEyeEngineVideoStreamEnabled(true);
        return true;
    } catch (const std::exception& e) {
        std::cerr << "❌ 初始化异常: " << e.what() << std::endl;
        g_video_publisher.reset();
        g_eyeengine_stream_enabled = false;
        return false;
    }
}

// 🆕 发布单帧视频（非阻塞）
void PublishCurrentFrame(const cv::Mat& frame) {
    // 🆕 检查视频流是否启用
    if (!g_enable_video_stream_to_eyeengine) {
        return;  // 视频流已禁用，直接返回
    }
    
    if (!g_video_publisher) {
        std::cerr << "[FaceReco] 视频发布器未初始化!" << std::endl;
        return;
    }
    
    if (frame.empty()) {
        return;
    }

    if (!g_eyeengine_stream_enabled) {
        EnsureEyeEngineVideoStreamEnabled();
    }
    
    try {
        // 确保是 BGR 格式
        cv::Mat bgr_frame;
        if (frame.channels() == 3 && frame.type() == CV_8UC3) {
            bgr_frame = frame;
        } else if (frame.channels() == 1) {
            cv::cvtColor(frame, bgr_frame, cv::COLOR_GRAY2BGR);
        } else {
            cv::cvtColor(frame, bgr_frame, cv::COLOR_RGB2BGR);
        }
        
        // 🆕 改用非阻塞发布方式，避免阻塞主循环
        nora::coms::video_stream::PublisherError result = g_video_publisher->TryPublishFrame(
            bgr_frame.data, bgr_frame.cols, bgr_frame.rows,
            nora::coms::video::PixelFormat::kBGR888
        );
        
        if (result == nora::coms::video_stream::PublisherError::kSuccess) {
            if (!first_frame_published) {
                publish_start_time = std::chrono::high_resolution_clock::now();
                first_frame_published = true;
            }
            publish_frame_count++;
            // 🆕 每 60 帧打印一次统计
            if (publish_frame_count % 60 == 0) {
                auto now = std::chrono::high_resolution_clock::now();
                auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - publish_start_time).count();
                // 🆕 FPS 计算基于最近 60 帧的耗时
                double fps = (60.0 * 1000.0) / (elapsed_ms > 0 ? elapsed_ms : 1);
                double error_rate = (publish_error_count * 100.0) / (60 + publish_error_count);
                std::cout << "📹 [FaceReco Publisher] 已发布帧=" << publish_frame_count 
                          << " FPS=" << std::fixed << std::setprecision(1) << fps
                          << " 丢弃率=" << std::fixed << std::setprecision(2) << error_rate << "%" << std::endl;
                
                // 重置统计，为了下一次 60 帧的局部 FPS 计算
                publish_start_time = now;
                publish_error_count = 0;
            }
        } else {
            // TryPublishFrame 失败（缓冲区满或其他错误），丢弃该帧继续处理（非阻塞）
            publish_error_count++;
            // 🆕 每 20 次错误打印一次（更频繁，便于调试）
            if (publish_error_count % 20 == 0) {
                std::cout << "⚠️  [FaceReco Publisher] 发布失败 " << publish_error_count 
                          << " 次 (错误码=" << static_cast<int>(result) << ")" 
                          << " 帧尺寸=" << bgr_frame.cols << "x" << bgr_frame.rows << std::endl;
            }
        }
    } catch (const std::exception& e) {
        publish_error_count++;
        // 继续运行，不中断 FaceReco 处理
    }
}

// 🆕 关闭视频发布器
void ShutdownVideoPublisher() {
    DisableEyeEngineVideoStream();
    if (g_video_publisher) {
        try {
            auto end = std::chrono::high_resolution_clock::now();
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - publish_start_time).count();
            double fps = (publish_frame_count * 1000.0) / (elapsed_ms > 0 ? elapsed_ms : 1);
            double error_rate = publish_frame_count > 0 ? (publish_error_count * 100.0) / publish_frame_count : 0;
            
            std::cout << "\n📊 FaceReco 发布器关闭统计:" << std::endl;
            std::cout << "  - 总发布帧数: " << publish_frame_count << std::endl;
            std::cout << "  - 总耗时: " << elapsed_ms << " ms" << std::endl;
            std::cout << "  - 平均吞吐量: " << std::fixed << std::setprecision(1) << fps << " FPS" << std::endl;
            std::cout << "  - 错误率: " << std::fixed << std::setprecision(2) << error_rate << "%" << std::endl;
            
            g_video_publisher->Shutdown();
            g_video_publisher.reset();
        } catch (const std::exception& e) {
            std::cerr << "⚠️  关闭发布器异常: " << e.what() << std::endl;
        }
    }
}

// ============ TRACKER AND RECOGNITION STATE ============
// Structure to hold per-tracker state
struct TrackedFace {
    int id;
    Bbox box;
    int last_seen_frame;
    int lost_frames;
    int recognition_attempts;
    bool recognition_disabled;
    bool prompted_for_registration;
    int detection_attempts;  // Count detection attempts before registering as new face
    std::string stable_face_id;
    std::string last_recognized_name;
};

// Global tracker management
static int next_tracker_id = 1;
static std::map<int, TrackedFace> active_trackers;

// Calculate Intersection over Union between two bboxes
float bboxIOU(const Bbox &a, const Bbox &b) {
    float x1_inter = std::max(a.x1, b.x1);
    float y1_inter = std::max(a.y1, b.y1);
    float x2_inter = std::min(a.x2, b.x2);
    float y2_inter = std::min(a.y2, b.y2);
    
    if (x2_inter < x1_inter || y2_inter < y1_inter) return 0.0f;
    
    float inter_area = (x2_inter - x1_inter) * (y2_inter - y1_inter);
    float a_area = (a.x2 - a.x1) * (a.y2 - a.y1);
    float b_area = (b.x2 - b.x1) * (b.y2 - b.y1);
    float union_area = a_area + b_area - inter_area;
    
    return union_area > 0.0f ? inter_area / union_area : 0.0f;
}

// Runtime-configurable parameters (defaults match previous hard-coded values)
bool largest_face_only = true;
bool record_face = true;
int distance_threshold = 90;
float face_thre = 0.40f;
float true_thre = 0.89f;
bool enable_liveness_detection = true;
int jump = 10;
int input_width = 320;
int input_height = 240;
int output_width = 320;
int output_height = 240;
float angle_threshold = 15.0f;
std::string project_path = std::string(PROJECT_PATH);

// Tracker and recognition parameters (P1-P2 optimizations)
bool enable_continuous_recognition = true;
int recognition_max_attempts = 3;
int track_max_lost = 15;
float track_iou_threshold = 0.3f;
int frame_skip_rate = 1;
int ncnn_num_threads = 2;
int detect_new_max_reco_times = 5;  // min attempts before registering as new face

// Stream/Performance controls
bool stream_only = false;           // only publish stream, skip detection/recognition
bool stream_publish_always = false;  // publish every frame when streaming is enabled
bool stream_enable_annotations = true;  // draw face boxes/labels on streamed frames

// Font / text rendering parameters (configurable via config.ini)
double font_scale_base = 0.3;               // baseline font scale
double font_scale_divisor = 40.0;           // box_width / divisor added to baseline
double font_scale_cap = 2.0;                // maximum font scale
double font_thickness_multiplier = 2.0;     // thickness = max(1, font_scale * multiplier)

// frame related computed values (will be updated from config at runtime)
cv::Size frame_size = Size(output_width,output_height);
float ratio_x = (float)output_width/ input_width;
float ratio_y = (float)output_height/ input_height;

std::vector<std::string> split(const std::string& s, char seperator)
{
   std::vector<std::string> output;

    std::string::size_type prev_pos = 0, pos = 0;

    while((pos = s.find(seperator, pos)) != std::string::npos)
    {
        std::string substring( s.substr(prev_pos, pos-prev_pos) );

        output.push_back(substring);

        prev_pos = ++pos;
    }

    output.push_back(s.substr(prev_pos, pos-prev_pos)); // Last word

    return output;
}

void calculateFaceDescriptorsFromDisk(Arcface & facereco,std::map<std::string,cv::Mat> & face_descriptors_map)
{
    std::string pattern_jpg = project_path + "/img/*.jpg";
	std::vector<cv::String> image_names;
    
	cv::glob(pattern_jpg, image_names);
    
    int image_number=image_names.size();

	if (image_number == 0) {
		std::cout << "No image files[jpg]" << std::endl;
        std::cout << "img 目录为空，先以空人脸库启动，等待后续自动注册。" << std::endl;
        return;
	}
    //cout <<"loading pictures..."<<endl;
    //cout <<"image number in total:"<<image_number<<endl;
    cv::Mat  face_img;
    unsigned int img_idx = 0;

  
    //convert to vector and store into fc, whcih is benefical to furthur operation
	for(auto const & img_name:image_names)
    {
        //cout <<"image name:"<<img_name<<endl;
        auto splitted_string = split(img_name,'/');
        auto splitted_string_2 = splitted_string[splitted_string.size()-1];
        std::size_t name_length = splitted_string_2.find_last_of('_');
        auto person_name =  splitted_string_2.substr(0,name_length);
        //std::cout<<person_name<<"\n";
        face_img = cv::imread(img_name);

        cv::Mat face_descriptor = facereco.getFeature(face_img);

        face_descriptors_map[person_name] = Statistics::zScore(face_descriptor);
        //cout << "now loading image " << ++img_idx << " out of " << image_number << endl;
        printf("\rloading[%.2lf%%]\n",  (++img_idx)*100.0 / (image_number));
    }
   
    cout <<"loading succeed! "<<image_number<<" pictures in total"<<endl;
    
}

// Simple descriptor cache (YAML) using OpenCV FileStorage
static bool loadDescriptorCache(const std::string & cache_path, std::map<std::string,cv::Mat> & out)
{
    if (cache_path.empty()) return false;
    if (!std::ifstream(cache_path)) return false;
    try {
        cv::FileStorage fs(cache_path, cv::FileStorage::READ);
        if (!fs.isOpened()) return false;
        cv::FileNode n = fs["descriptors"];
        if (n.type() == cv::FileNode::SEQ) {
            // descriptors stored as a sequence of { id: <name>, mat: <cv::Mat> }
            for (auto it = n.begin(); it != n.end(); ++it) {
                std::string name;
                cv::Mat mat;
                (*it)["id"] >> name;
                (*it)["mat"] >> mat;
                if (!name.empty() && !mat.empty()) out[name] = mat;
            }
        } else if (n.type() == cv::FileNode::MAP) {
            // backward-compatible: descriptors stored as a map with name keys
            for (auto it = n.begin(); it != n.end(); ++it) {
                std::string name = (*it).name();
                cv::Mat mat;
                (*it) >> mat;
                if (!name.empty() && !mat.empty()) out[name] = mat;
            }
        }
            fs.release();
            if (out.empty()) {
                // treat an empty descriptors section as load failure so caller can recompute
                std::cout << "Descriptor cache parsed but contains 0 entries: " << cache_path << std::endl;
                return false;
            }
            std::cout << "Loaded descriptor cache: " << cache_path << " (" << out.size() << " entries)" << std::endl;
            return true;
    } catch (const std::exception &e) {
        std::cerr << "[WARN] loadDescriptorCache failed: " << e.what() << std::endl;
        return false;
    }
}

static bool saveDescriptorCache(const std::string & cache_path, const std::map<std::string,cv::Mat> & in)
{
    if (cache_path.empty()) return false;
    try {
        // write to a temporary file first, then atomically rename
        std::string tmp_path = cache_path + ".tmp";
        cv::FileStorage fs(tmp_path, cv::FileStorage::WRITE);
        // write as a sequence of { id: <name>, mat: <cv::Mat> } entries
        fs << "descriptors" << "[";
        for (const auto & p : in) {
            fs << "{";
            fs << "id" << p.first;
            fs << "mat" << p.second;
            fs << "}";
        }
        fs << "]";
        fs.release();
        // rename tmp -> final (overwrite if exists)
        if (std::rename(tmp_path.c_str(), cache_path.c_str()) != 0) {
            // best-effort: remove tmp file if rename failed
            std::perror("rename");
            std::remove(tmp_path.c_str());
            return false;
        }
        std::cout << "Saved descriptor cache: " << cache_path << " (" << in.size() << " entries)" << std::endl;
        return true;
    } catch (const std::exception &e) {
        std::cerr << "[WARN] saveDescriptorCache failed: " << e.what() << std::endl;
        return false;
    }
}


static bool isFourDigitFaceId(const std::string& value)
{
    return value.size() == 4 && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isdigit(ch) != 0;
    });
}

static std::string generateNextFourDigitFaceId(const doly::vision::FaceDatabase& face_db,
                                               const std::map<std::string,cv::Mat>& face_descriptors_map)
{
    std::set<int> used_ids;
    for (const auto& record : face_db.list()) {
        if (isFourDigitFaceId(record.face_id)) {
            used_ids.insert(std::stoi(record.face_id));
        }
    }
    for (const auto& [descriptor_id, _] : face_descriptors_map) {
        if (isFourDigitFaceId(descriptor_id)) {
            used_ids.insert(std::stoi(descriptor_id));
        }
    }

    for (int candidate = 1; candidate <= 9999; ++candidate) {
        if (used_ids.find(candidate) != used_ids.end()) {
            continue;
        }
        std::ostringstream oss;
        oss << std::setw(4) << std::setfill('0') << candidate;
        return oss.str();
    }

    return std::string();
}

static std::string buildFaceImagePath(const std::string& face_id)
{
    return project_path + "/img/" + face_id + "_0.jpg";
}

static std::string currentIsoTimestamp()
{
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

// Async saver for heavy disk writes (imwrite) to avoid blocking main loop
class AsyncSaver {
public:
    AsyncSaver() : stop_(false) { worker_ = std::thread(&AsyncSaver::workerLoop, this); }
    ~AsyncSaver() { {
            std::unique_lock<std::mutex> lk(mtx_);
            stop_ = true; cv_.notify_one(); }
        if (worker_.joinable()) worker_.join(); }

    void saveImageAsync(const std::string & path, const cv::Mat & img) {
        std::unique_lock<std::mutex> lk(mtx_);
        queue_.emplace_back(path, img.clone());
        cv_.notify_one();
    }

private:
    void workerLoop() {
        while (true) {
            std::pair<std::string, cv::Mat> item;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [&]{ return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) break;
                item = std::move(queue_.front()); queue_.pop_front();
            }
            try { cv::imwrite(item.first, item.second); }
            catch(...) { }
        }
    }

    std::thread worker_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<std::pair<std::string, cv::Mat>> queue_;
    bool stop_;
};
void calculateFaceDescriptorsFromImgDataset(Arcface & facereco,std::map<std::string,std::list<cv::Mat>> & img_dataset,std::map<std::string, std::list<cv::Mat>> & face_descriptors_map)
{
    int img_idx = 0;
    const int image_number = img_dataset.size()*5;
    for(const auto & dataset_pair:img_dataset)
    {
        const std::string person_name = dataset_pair.first;

        std::list<cv::Mat> descriptors;
        if (image_number == 0) {
            cout << "No image files[jpg]" << endl;
            return;
        }
        else{
            cout <<"loading pictures..."<<endl;
            for(const auto & face_img:dataset_pair.second)
            {
                cv::Mat face_descriptor = facereco.getFeature(face_img);
                descriptors.push_back( Statistics::zScore(face_descriptor));
                cout << "now loading image " << ++img_idx << " out of " << image_number << endl;
                //printf("\rloading[%.2lf%%]\n",  (++img_idx)*100.0 / (image_number));
            }
            face_descriptors_map[person_name] = std::move(descriptors);
        }
        
    }
}
void loadLiveModel( Live & live )
{
    //Live detection configs
    struct ModelConfig config1 ={2.7f,0.0f,0.0f,80,80,"model_1",false};
    struct ModelConfig config2 ={4.0f,0.0f,0.0f,80,80,"model_2",false};
    vector<struct ModelConfig> configs;
    configs.emplace_back(config1);
    configs.emplace_back(config2);
    live.LoadModel(configs);
}
cv::Mat createFaceLandmarkGTMatrix()
{
    // groundtruth face landmark
    float v1[5][2] = {
            {30.2946f, 51.6963f},
            {65.5318f, 51.5014f},
            {48.0252f, 71.7366f},
            {33.5493f, 92.3655f},
            {62.7299f, 92.2041f}};

    cv::Mat src(5, 2, CV_32FC1, v1); 
    memcpy(src.data, v1, 2 * 5 * sizeof(float));
    return src.clone();
}
cv::Mat createFaceLandmarkMatrixfromBBox(const Bbox  & box)
{
    float v2[5][2] =
                {{box.ppoint[0], box.ppoint[5]},
                {box.ppoint[1], box.ppoint[6]},
                {box.ppoint[2], box.ppoint[7]},
                {box.ppoint[3], box.ppoint[8]},
                {box.ppoint[4], box.ppoint[9]},
                };
    cv::Mat dst(5, 2, CV_32FC1, v2);
    memcpy(dst.data, v2, 2 * 5 * sizeof(float));

    return dst.clone();
}

// Helper function to extract landmarks from Bbox for angle calculation
inline void extractLandmarksFromBBox(const Bbox& box, float landmark[5][2]) {
    landmark[0][0] = box.ppoint[0];
    landmark[0][1] = box.ppoint[5];
    landmark[1][0] = box.ppoint[1];
    landmark[1][1] = box.ppoint[6];
    landmark[2][0] = box.ppoint[2];
    landmark[2][1] = box.ppoint[7];
    landmark[3][0] = box.ppoint[3];
    landmark[3][1] = box.ppoint[8];
    landmark[4][0] = box.ppoint[4];
    landmark[4][1] = box.ppoint[9];
}

Bbox  getLargestBboxFromBboxVec(const std::vector<Bbox> & faces_info)
{
    if(faces_info.size()>0)
    {
        int lagerest_face=0,largest_number=0;
        for (int i = 0; i < faces_info.size(); i++){
            int y_ = (int) faces_info[i].y2 * ratio_y;
            int h_ = (int) faces_info[i].y1 * ratio_y;
            if (h_-y_> lagerest_face){
                lagerest_face=h_-y_;
                largest_number=i;                   
            }
        }
        
        return faces_info[largest_number];
    }
    return Bbox();
}

LiveFaceBox Bbox2LiveFaceBox(const Bbox  & box)
{
    float x_   =  box.x1;
    float y_   =  box.y1;
    float x2_ =  box.x2;
    float y2_ =  box.y2;
    int x = (int) x_ ;
    int y = (int) y_;
    int x2 = (int) x2_;
    int y2 = (int) y2_;
    struct LiveFaceBox  live_box={x_,y_,x2_,y2_} ;
    return live_box;
}

cv::Mat alignFaceImage(const cv::Mat & frame, const Bbox & bbox,const cv::Mat & gt_landmark_matrix)
{
    cv::Mat face_landmark = createFaceLandmarkMatrixfromBBox(bbox);

    cv::Mat transf = FacePreprocess::similarTransform(face_landmark, gt_landmark_matrix);

    cv::Mat aligned = frame.clone();
    cv::warpPerspective(frame, aligned, transf, cv::Size(96, 112), INTER_LINEAR);
    resize(aligned, aligned, Size(112, 112), 0, 0, INTER_LINEAR);
     
    return aligned.clone();
}

std::string  getClosestFaceDescriptorPersonName(std::map<std::string,cv::Mat> & disk_face_descriptors, cv::Mat face_descriptor)
{
    // guard: no descriptors available
    if (disk_face_descriptors.empty()) return std::string("");

    vector<double> score_(disk_face_descriptors.size());
    std::vector<std::string> labels;
    int i = 0;
    for(const auto & disk_descp:disk_face_descriptors)
    {
        score_[i] = (Statistics::cosineDistance(disk_descp.second, face_descriptor));
        labels.push_back(disk_descp.first);
        i++;
    }
    int maxPosition = max_element(score_.begin(),score_.end()) - score_.begin(); 
    int pos = score_[maxPosition]>face_thre?maxPosition:-1;
    //cout << "score_[maxPosition] " << score_[maxPosition] << endl;
    std::string person_name = "";
    if(pos>=0)
    {
        person_name = labels[pos];
    }
    score_.clear();

    return person_name;
}

static std::pair<std::string, double> getClosestFaceDescriptorMatch(
    std::map<std::string,cv::Mat> & disk_face_descriptors,
    const cv::Mat& face_descriptor)
{
    if (disk_face_descriptors.empty()) {
        return {std::string(), 0.0};
    }

    double best_score = std::numeric_limits<double>::lowest();
    std::string best_label;
    for (const auto& disk_descp : disk_face_descriptors) {
        const double score = Statistics::cosineDistance(disk_descp.second, face_descriptor);
        if (score > best_score) {
            best_score = score;
            best_label = disk_descp.first;
        }
    }

    if (best_score > face_thre) {
        return {best_label, best_score};
    }

    return {std::string(), best_score};
}

std::string  getClosestFaceDescriptorPersonName(std::map<std::string,std::list<cv::Mat>> & disk_face_descriptors, cv::Mat face_descriptor)
{
    // guard: no descriptors available
    if (disk_face_descriptors.empty()) return std::string("");

    vector<std::list<double>> score_(disk_face_descriptors.size());

    std::vector<std::string> labels;

    int i = 0;

    for(const auto & disk_descp:disk_face_descriptors)
    {
        for(const auto & descriptor:disk_descp.second)
        {
            score_[i].push_back(Statistics::cosineDistance(descriptor, face_descriptor));
        }

        labels.push_back(disk_descp.first);
        i++;
    
    }

    int maxPosition = max_element(score_.begin(),score_.end()) - score_.begin();
    
    auto get_max_from_score_list = 
                            [&]()
                            {
                                double max = *score_[maxPosition].begin();
                                for(const auto & elem:score_[maxPosition])
                                {
                                    if(max<elem)
                                    {
                                        max = elem;
                                    }
                                }
                                return max;
                            }; 

    double max = get_max_from_score_list();

    int pos = max>face_thre?maxPosition:-1;

    std::string person_name = "";
    if(pos>=0)
    {
        person_name = labels[pos];
    }

    score_.clear();

    return person_name;
}

int MTCNNDetection(doly::vision::RuntimeControl& control,
                   doly::vision::VisionBusBridge& bus,
                   doly::vision::RuntimeMetrics& metrics)
{
    //OpenCV Version
    cout << "OpenCV Version: " << CV_MAJOR_VERSION << "."
    << CV_MINOR_VERSION << "."
    << CV_SUBMINOR_VERSION << endl;

    Arcface facereco;

    SetVisionRuntimeContext(&control, &bus, &metrics);
    metrics.fps.store(0.0);
    metrics.active_tracks.store(0);
    metrics.recognized_faces.store(0);

    // load the dataset and store it inside a dictionary
    std::map<std::string,cv::Mat> face_descriptors_dict;

    // 🆕 从 FaceDatabase 加载元数据
    std::string db_path = project_path + "/data/face_db.json";
    doly::vision::FaceDatabase face_db(db_path);
    if (face_db.load()) {
        auto faces = face_db.list();
        std::cout << "[FaceDB] ✅ 加载了 " << faces.size() << " 条人脸记录" << std::endl;
        // 打印每条记录
        for (const auto& face : faces) {
            std::cout << "  [FaceDB] 👤 " << face.name << " (" << face.face_id << ")" 
                      << " - 样本数: " << face.sample_count << std::endl;
        }
    } else {
        std::cout << "[FaceDB] ⚠️  face_db.json 不存在或为空，将在注册时创建新数据库" << std::endl;
    }

    auto get_face_db_mtime = [&db_path]() -> long long {
        std::error_code ec;
        auto time = std::filesystem::last_write_time(db_path, ec);
        if (ec) {
            return 0;
        }
        return static_cast<long long>(time.time_since_epoch().count());
    };
    long long face_db_mtime = get_face_db_mtime();
    auto reload_face_db_if_changed = [&](bool force = false) {
        long long current_mtime = get_face_db_mtime();
        if (!force && current_mtime == face_db_mtime) {
            return true;
        }
        if (!face_db.load()) {
            std::cerr << "[FaceDB] ❌ 重新加载 face_db.json 失败: " << db_path << std::endl;
            return false;
        }
        face_db_mtime = current_mtime;
        return true;
    };

    // Descriptor cache settings
    bool enable_descriptor_cache = Settings::getBool("enable_descriptor_cache", true);
    std::string descriptor_cache_path = Settings::getString("descriptor_cache_path", project_path + "/descriptors.yml");

    bool cache_loaded = false;
    if (enable_descriptor_cache) {
        cache_loaded = loadDescriptorCache(descriptor_cache_path, face_descriptors_dict);
    }

    if (!cache_loaded) {
        // fallback: compute descriptors from disk and optionally save cache
        calculateFaceDescriptorsFromDisk(facereco, face_descriptors_dict);
        if (enable_descriptor_cache) {
            // best-effort save (may overwrite)
            saveDescriptorCache(descriptor_cache_path, face_descriptors_dict);
        }
    }

    auto trim_copy = [](std::string value) {
        const std::string whitespace = " \t\r\n";
        const std::size_t begin = value.find_first_not_of(whitespace);
        if (begin == std::string::npos) {
            return std::string();
        }
        const std::size_t end = value.find_last_not_of(whitespace);
        return value.substr(begin, end - begin + 1);
    };

    auto register_face_record = [&](TrackedFace& tracker,
                                    const cv::Mat& aligned_img,
                                    const cv::Mat& face_descriptor,
                                    const Bbox& bbox,
                                    float liveness_confidence,
                                    float face_angle,
                                    const std::string& register_name,
                                    const std::string& register_source,
                                    std::string* registered_face_id = nullptr) -> bool {
        reload_face_db_if_changed(true);

        const std::string generated_face_id = generateNextFourDigitFaceId(face_db, face_descriptors_dict);
        if (generated_face_id.empty()) {
            std::cerr << "[FaceDB] ❌ 无法分配新的4位人脸编号" << std::endl;
            return false;
        }

        const std::string image_path = buildFaceImagePath(generated_face_id);
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(image_path).parent_path(), ec);

        doly::vision::FaceRecord record;
        record.face_id = generated_face_id;
        record.name = register_name.empty() ? generated_face_id : register_name;
        record.image_path = image_path;
        record.created_at = currentIsoTimestamp();
        record.last_seen = record.created_at;
        record.sample_count = 1;
        record.metadata = {
            {"tracker_id", tracker.id},
            {"register_source", register_source},
            {"bbox", {
                {"x1", bbox.x1 * ratio_x},
                {"y1", bbox.y1 * ratio_y},
                {"x2", bbox.x2 * ratio_x},
                {"y2", bbox.y2 * ratio_y}
            }},
            {"liveness", true},
            {"confidence", liveness_confidence},
            {"angle", face_angle}
        };

        if (!face_db.addOrUpdate(record) || !face_db.save()) {
            std::cerr << "[FaceDB] ❌ 注册新人脸失败: " << generated_face_id << std::endl;
            return false;
        }

        face_db_mtime = get_face_db_mtime();
        static AsyncSaver saver;
        saver.saveImageAsync(image_path, aligned_img);
        face_descriptors_dict[generated_face_id] = face_descriptor;
        if (enable_descriptor_cache) {
            saveDescriptorCache(descriptor_cache_path, face_descriptors_dict);
        }

        tracker.stable_face_id = generated_face_id;
        tracker.last_recognized_name = record.name;
        tracker.recognition_disabled = true;
        tracker.detection_attempts = 0;
        tracker.recognition_attempts = 0;

        if (registered_face_id) {
            *registered_face_id = generated_face_id;
        }

        std::cout << "[TRACK " << tracker.id << "] registered as: "
                  << record.name << " (" << generated_face_id << ") -> " << image_path << std::endl;
        return true;
    };

    Live live;

    float factor = 0.709f;
    float threshold[3] = {0.7f, 0.6f, 0.6f};

    PiCamera camera;

    // ✅ 加载 YAML 配置（替代 config.ini）
    std::string yaml_config_path = project_path + "/config/vision_service.yaml";
    if(!YAMLConfig::load(yaml_config_path)) {
        std::cerr << "[LiveFaceReco] ❌ 无法加载配置文件: " << yaml_config_path << std::endl;
        return -1;
    }
    
    std::cout << "[LiveFaceReco] ✅ 使用 YAML 配置系统" << std::endl;

        // 从 YAML 读取应用级配置
        largest_face_only = YAMLConfig::getBool("face_detection.largest_face_only", largest_face_only);
        record_face = YAMLConfig::getBool("face_detection.record_face", record_face);
        distance_threshold = YAMLConfig::getInt("face_recognition.threshold", distance_threshold);
        face_thre = YAMLConfig::getDouble("face_detection.detection_threshold", face_thre);
        enable_liveness_detection = YAMLConfig::getBool("liveness_detection.enabled", enable_liveness_detection);
        true_thre = YAMLConfig::getDouble("liveness_detection.threshold", true_thre);
        jump = YAMLConfig::getInt("liveness_detection.jump", jump);
        input_width = YAMLConfig::getInt("image_processing.input_width", input_width);
        input_height = YAMLConfig::getInt("image_processing.input_height", input_height);
        output_width = YAMLConfig::getInt("image_processing.output_width", output_width);
        output_height = YAMLConfig::getInt("image_processing.output_height", output_height);
        angle_threshold = YAMLConfig::getDouble("face_angle.threshold", angle_threshold);
        project_path = YAMLConfig::getString("project.path", project_path);
        
        // 加载 P1-P2 优化参数
        enable_continuous_recognition = YAMLConfig::getBool("face_recognition.enable_continuous", enable_continuous_recognition);
        recognition_max_attempts = YAMLConfig::getInt("face_recognition.max_attempts", recognition_max_attempts);
        track_max_lost = YAMLConfig::getInt("face_tracking.max_lost_frames", track_max_lost);
        track_iou_threshold = YAMLConfig::getDouble("face_tracking.iou_threshold", track_iou_threshold);
        frame_skip_rate = YAMLConfig::getInt("face_detection.frame_skip_rate", frame_skip_rate);
        ncnn_num_threads = YAMLConfig::getInt("face_detection.ncnn_num_threads", ncnn_num_threads);
        detect_new_max_reco_times = YAMLConfig::getInt("face_detection.detect_new_max_reco_times", detect_new_max_reco_times);
    const bool auto_register_new_face = Settings::getBool(
        "auto_register_new_face",
        YAMLConfig::getBool("face_detection.auto_register_new_face", false));
    const bool interactive_register_face = IsInteractiveRegisterMode();
    stream_only = Settings::getBool("vision_stream_only", YAMLConfig::getBool("video_stream.stream_only", stream_only));
    stream_publish_always = Settings::getBool(
        "vision_stream_publish_always",
        YAMLConfig::getBool("video_stream.publish_always", g_enable_video_stream_to_eyeengine));
    stream_enable_annotations = Settings::getBool("vision_stream_enable_annotations", YAMLConfig::getBool("video_stream.enable_annotations", stream_enable_annotations));
    // 字体渲染配置
    font_scale_base = YAMLConfig::getDouble("text_rendering.font_scale_base", font_scale_base);
    font_scale_divisor = YAMLConfig::getDouble("text_rendering.font_scale_divisor", font_scale_divisor);
    font_scale_cap = YAMLConfig::getDouble("text_rendering.font_scale_cap", font_scale_cap);
    font_thickness_multiplier = YAMLConfig::getDouble("text_rendering.font_thickness_multiplier", font_thickness_multiplier);
        
    std::cout << "[CONFIG] enable_continuous_recognition=" << enable_continuous_recognition << std::endl;
    std::cout << "[CONFIG] recognition_max_attempts=" << recognition_max_attempts << std::endl;
    std::cout << "[CONFIG] track_max_lost=" << track_max_lost << std::endl;
    std::cout << "[CONFIG] track_iou_threshold=" << track_iou_threshold << std::endl;
    std::cout << "[CONFIG] frame_skip_rate=" << frame_skip_rate << std::endl;
    std::cout << "[CONFIG] ncnn_num_threads=" << ncnn_num_threads << std::endl;
    std::cout << "[CONFIG] detect_new_max_reco_times=" << detect_new_max_reco_times << std::endl;
    std::cout << "[CONFIG] auto_register_new_face=" << (auto_register_new_face ? 1 : 0) << std::endl;
    std::cout << "[CONFIG] interactive_register_face=" << (interactive_register_face ? 1 : 0) << std::endl;
    std::cout << "[CONFIG] enable_liveness_detection=" << (enable_liveness_detection ? 1 : 0) << std::endl;
    std::cout << "[CONFIG] stream_only=" << (stream_only ? 1 : 0) << std::endl;
    std::cout << "[CONFIG] stream_publish_always=" << (stream_publish_always ? 1 : 0) << std::endl;
    std::cout << "[CONFIG] stream_enable_annotations=" << (stream_enable_annotations ? 1 : 0) << std::endl;

    if (enable_liveness_detection) {
        loadLiveModel(live);
    }

    // apply camera caps from config
    int cfg_w = Settings::getInt("cap_frame_width", 320);
    int cfg_h = Settings::getInt("cap_frame_height", 240);
    int cfg_f = Settings::getInt("cap_fps", 20);
    const unsigned int camera_frame_timeout_ms = 1500;

    // recompute dependent values (ratios and frame size)
    frame_size = Size(output_width, output_height);
    ratio_x = (float)output_width / (float)input_width;
    ratio_y = (float)output_height / (float)input_height;

    camera.options->camera = 0;
    camera.options->timeout = 1000;
    camera.options->verbose = false;
    camera.options->photo_width = static_cast<unsigned int>(cfg_w);
    camera.options->photo_height = static_cast<unsigned int>(cfg_h);
    camera.options->video_width = static_cast<unsigned int>(cfg_w);
    camera.options->video_height = static_cast<unsigned int>(cfg_h);
    camera.options->framerate = static_cast<float>(std::max(cfg_f, 1));

    bool capture_active = false;
    const int camera_restart_cooldown_ms = std::max(Settings::getInt("camera_restart_cooldown_ms", 200), 0);
    auto last_camera_stop_ts = std::chrono::steady_clock::time_point::min();

    auto stop_camera_video = [&](bool apply_cooldown = true) {
        try {
            camera.stopVideo();
        } catch (const std::exception& e) {
            std::cerr << "[WARN] PiCamera stopVideo failed: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "[WARN] PiCamera stopVideo failed: unknown exception" << std::endl;
        }
        capture_active = false;
        last_camera_stop_ts = std::chrono::steady_clock::now();
        if (apply_cooldown && camera_restart_cooldown_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(camera_restart_cooldown_ms));
        }
    };

    auto ensure_camera_video_started = [&]() -> bool {
        if (capture_active) {
            return true;
        }

        if (last_camera_stop_ts != std::chrono::steady_clock::time_point::min() && camera_restart_cooldown_ms > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - last_camera_stop_ts).count();
            if (elapsed < camera_restart_cooldown_ms) {
                std::this_thread::sleep_for(std::chrono::milliseconds(camera_restart_cooldown_ms - elapsed));
            }
        }

        for (int attempt = 1; attempt <= 2; ++attempt) {
            try {
                if (attempt > 1) {
                    stop_camera_video(false);
                    if (camera_restart_cooldown_ms > 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(camera_restart_cooldown_ms * attempt));
                    }
                }

                capture_active = camera.startVideo();
                if (capture_active) {
                    std::cout << "[INFO] PiCamera video started (attempt " << attempt << ")." << std::endl;
                    return true;
                }

                std::cerr << "[WARN] PiCamera startVideo failed (attempt " << attempt << ")" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "[WARN] PiCamera startVideo failed (attempt " << attempt << "): " << e.what() << std::endl;
                capture_active = false;
            }
        }

        return false;
    };

    float confidence;
    vector<float> fps;
    
    Mat frame;
    Mat result_cnn;

    double score, angle;

    cv::Mat face_landmark_gt_matrix = createFaceLandmarkGTMatrix();
    int count = -1;
    std::string liveface;
    float ratio_x = 1;
    float ratio_y = 1;
    int flag = 0;
    int record_count = 0;
    int file_save_count = 0;

    bool video_recording = false;
    cv::VideoWriter video_writer;
    std::string video_request_id;
    std::string video_file_path;
    bool video_include_annotations = false;
    int video_fps = 15;
    int video_max_duration_s = 0;
    uint64_t video_start_ms = 0;

    // FPS measurement
    auto last_ts = std::chrono::high_resolution_clock::now();
    double fps_smoothed = 0.0;

    // 从配置文件读取 GUI 设置
    bool enable_gui = false;
    const bool allow_interactive_registration = ::isatty(STDIN_FILENO);
    if (interactive_register_face && !allow_interactive_registration) {
        std::cerr << "[LiveFaceReco] ❌ register_face 模式需要交互式终端" << std::endl;
        return -1;
    }
    
    // 首先尝试从 YAML 配置文件读取 gui.enabled 设置
    ConfigManager gui_config;
    if (gui_config.LoadFromYAML("/home/pi/dolydev/libs/FaceReco/config/vision_service.yaml")) {
        enable_gui = gui_config.GetBool("gui", "enabled", false);
        if (enable_gui) {
            std::cout << "[INFO] GUI enabled from config file" << std::endl;
        } else {
            std::cout << "[INFO] GUI disabled from config file (gui.enabled = false)" << std::endl;
        }
    } else {
        // 如果 YAML 加载失败，退回到环境变量检查
        const char * disp_env = std::getenv("DISPLAY");
        if (disp_env != nullptr && std::strlen(disp_env) > 0) {
            enable_gui = true;
            std::cout << "[INFO] GUI enabled via DISPLAY environment variable" << std::endl;
        } else {
            std::cout << "[INFO] No DISPLAY found. GUI disabled; running headless." << std::endl;
        }
    }
    
    // 创建显示窗口（如果启用 GUI）
    if (enable_gui) {
        try {
            cv::namedWindow("LiveFaceReco", cv::WINDOW_NORMAL);
            std::cout << "[INFO] Display window created successfully" << std::endl;
        } catch (const std::exception &e) {
            std::cerr << "[WARN] Failed to create display window: " << e.what() << std::endl;
            enable_gui = false;
        }
    }
    
    while(!control.isShutdownRequested())
    {
        json faces_array = json::array();
        json primary_face_data;
        bool has_primary_face = false;

        bool capture_work_pending = video_recording || control.hasPendingCaptureRequest();

        if (!control.isEnabled() && !capture_work_pending) {
            if (capture_active) {
                stop_camera_video();
            }
            if (!control.waitUntilEnabledOrCaptureRequested()) {
                break;
            }
            continue;
        }

        if (!capture_active) {
            if (!ensure_camera_video_started()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
        }


        ++count;
        if (!camera.getVideoFrame(frame, camera_frame_timeout_ms)) {
            std::cerr << "[WARN] PiCamera getVideoFrame timeout/fail" << std::endl;
            stop_camera_video();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if(frame.empty()) {
            continue;
        }

        cv::Mat stream_frame = frame;
        if (output_width > 0 && output_height > 0 &&
            (frame.cols != output_width || frame.rows != output_height)) {
            cv::resize(frame, stream_frame, cv::Size(output_width, output_height));
        }

        bool mode_ready = (!g_bus_bridge) ? true : g_bus_bridge->hasModeSignal();
        if (!mode_ready && !capture_work_pending) {
            if (enable_gui) cv::imshow("LiveFaceReco", frame);
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
            continue;
        }

        bool should_detect = control.isEnabled() && !stream_only && (count % frame_skip_rate == 0);
        
        if (!should_detect) {
            if (enable_gui && !frame.empty()) cv::imshow("LiveFaceReco", frame);
            if (enable_gui) {
                int k = cv::waitKey(33);
                if (k == 27) break;
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(33));
            }
        }
        
        // compute FPS
        auto now_ts = std::chrono::high_resolution_clock::now();
        double elapsed_s = std::chrono::duration_cast<std::chrono::microseconds>(now_ts - last_ts).count() / 1e6;
        last_ts = now_ts;
        double fps_now = elapsed_s > 0.0 ? 1.0 / elapsed_s : 0.0;
        // smooth FPS for nicer output
        if (fps_smoothed <= 0.0) fps_smoothed = fps_now; else fps_smoothed = fps_smoothed * 0.9 + fps_now * 0.1;
    metrics.fps.store(fps_smoothed);
        // print to console
        std::ostringstream oss_fps; oss_fps << std::fixed << std::setprecision(2) << fps_smoothed << " FPS";
        // overlay on frame
        cv::putText(frame, oss_fps.str(), cv::Point(10,20), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0,255,0), 2);
        
        flag = 0;
        
        if (should_detect) {
        // ========== DETECT FACES ==========
        std::vector<Bbox> faces_info = detect_mtcnn(frame);
        std::map<int, int> tracker_matched;  // Maps tracker_id -> detection_index
        
        if(faces_info.size() >= 1)
        {
            flag = 1;
            auto large_box = getLargestBboxFromBboxVec(faces_info);
            
            // ========== IOU-BASED TRACKER MATCHING ==========
            int best_tracker_id = -1;
            float best_iou = track_iou_threshold;
            
            for (auto& [tracker_id, tracker] : active_trackers) {
                float iou = bboxIOU(tracker.box, large_box);
                if (iou > best_iou) {
                    best_iou = iou;
                    best_tracker_id = tracker_id;
                }
            }
            
            // ========== CREATE OR UPDATE TRACKER ==========
            if (best_tracker_id >= 0) {
                // Update existing tracker
                // std::cout << "[TRACK " << best_tracker_id << "] matched with IOU=" << best_iou << std::endl;
                active_trackers[best_tracker_id].box = large_box;
                active_trackers[best_tracker_id].last_seen_frame = count;
                active_trackers[best_tracker_id].lost_frames = 0;
                tracker_matched[best_tracker_id] = 0;
            } else {
                // Create new tracker
                TrackedFace new_tracker;
                new_tracker.id = next_tracker_id++;
                new_tracker.box = large_box;
                new_tracker.last_seen_frame = count;
                new_tracker.lost_frames = 0;
                new_tracker.recognition_attempts = 0;
                new_tracker.recognition_disabled = false;
                new_tracker.prompted_for_registration = false;
                new_tracker.detection_attempts = 0;  // Initialize detection attempts counter
                new_tracker.stable_face_id = "";
                new_tracker.last_recognized_name = "";
                active_trackers[new_tracker.id] = new_tracker;
                best_tracker_id = new_tracker.id;
                tracker_matched[best_tracker_id] = 0;
                std::cout << "[TRACK " << best_tracker_id << "] created (new face detected)" << std::endl;
            }
            
            // ========== RECOGNITION LOGIC FOR ACTIVE TRACKER ==========
            if (best_tracker_id >= 0) {
                TrackedFace &cur_tracker = active_trackers[best_tracker_id];
                confidence = enable_liveness_detection ? 0.0f : 1.0f;
                
                if (g_bus_bridge) {
                    double x1_px = large_box.x1 * ratio_x;
                    double y1_px = large_box.y1 * ratio_y;
                    double x2_px = large_box.x2 * ratio_x;
                    double y2_px = large_box.y2 * ratio_y;

                    double frame_w = static_cast<double>(frame.cols);
                    double frame_h = static_cast<double>(frame.rows);
                    double center_x = ((x1_px + x2_px) * 0.5) / frame_w;
                    double center_y = ((y1_px + y2_px) * 0.5) / frame_h;
                    center_x = std::clamp(center_x, 0.0, 1.0);
                    center_y = std::clamp(center_y, 0.0, 1.0);

                    json primary_face;
                    primary_face["id"] = cur_tracker.id;
                    primary_face["bbox"] = {
                        {"x1", x1_px},
                        {"y1", y1_px},
                        {"x2", x2_px},
                        {"y2", y2_px}
                    };
                    primary_face["normalized"] = {
                        {"x", center_x},
                        {"y", center_y}
                    };
                    primary_face["confidence"] = confidence;
                    primary_face["name"] = cur_tracker.last_recognized_name;
                    primary_face["face_id"] = cur_tracker.stable_face_id;
                    primary_face["liveness"] = !enable_liveness_detection || confidence > true_thre;
                    primary_face["angle"] = angle;

                    faces_array.push_back(primary_face);
                    primary_face_data = primary_face;
                    has_primary_face = true;

                    json event_payload;
                    event_payload["event"] = "face_detected";
                    event_payload["count"] = static_cast<int>(faces_info.size());
                    event_payload["faces"] = faces_array;
                    event_payload["primary"] = primary_face;
                    event_payload["frame_size"] = {
                        {"width", frame.cols},
                        {"height", frame.rows}
                    };
                    g_bus_bridge->publishFaceSnapshot(event_payload);
                }
                
                LiveFaceBox live_face_box = Bbox2LiveFaceBox(large_box);
                cv::Mat aligned_img = alignFaceImage(frame, large_box, face_landmark_gt_matrix);
                
                // Calculate face angle
                float landmarks[5][2];
                extractLandmarksFromBBox(large_box, landmarks);
                angle = Statistics::countAngle(landmarks);
                
                // Determine if we should attempt recognition
                bool should_recognize = false;
                const bool recognition_mode_enabled = (!g_bus_bridge) ? true : g_bus_bridge->isRecognitionAllowed();
                const int max_unknown_attempts = (interactive_register_face || auto_register_new_face)
                    ? std::max(recognition_max_attempts, detect_new_max_reco_times)
                    : recognition_max_attempts;
                if (recognition_mode_enabled) {
                    should_recognize = !cur_tracker.recognition_disabled &&
                        (enable_continuous_recognition || cur_tracker.recognition_attempts < max_unknown_attempts);
                }
                
                // Check angle threshold
                if (angle > angle_threshold) {
                    // std::cout << "[TRACK " << cur_tracker.id << "] Face angle (" << angle 
                    //           << "°) exceeds threshold (" << angle_threshold << "°). Skipping." << std::endl;
                    liveface = "Face angle too large";
                    cv::putText(frame, liveface, cv::Point(15, 40), 1, 2.0, cv::Scalar(255, 0, 0));
                } else if (should_recognize) {
                    // Perform recognition
                    cv::Mat face_descriptor = facereco.getFeature(aligned_img);
                    face_descriptor = Statistics::zScore(face_descriptor);
                    
                    bool is_real_face = true;
                    if (enable_liveness_detection) {
                        confidence = live.Detect(frame, live_face_box);
                        is_real_face = (confidence > true_thre);

                        if (confidence <= true_thre) {
                            liveface = "Fake face!!";
                        } else {
                            liveface = "True face";
                        }
                    } else {
                        confidence = 1.0f;
                        liveface = "Face detected";
                    }
                    
                    auto match_result = getClosestFaceDescriptorMatch(face_descriptors_dict, face_descriptor);
                    std::string matched_face_id = match_result.first;
                    const double match_score = match_result.second;
                    std::string person_name = matched_face_id;

                    if (!matched_face_id.empty()) {
                        reload_face_db_if_changed();
                        auto matched_record = face_db.get(matched_face_id);
                        if (!matched_record.has_value()) {
                            matched_record = face_db.getByName(matched_face_id);
                        }
                        if (matched_record.has_value()) {
                            matched_face_id = matched_record->face_id;
                            person_name = matched_record->name.empty() ? matched_record->face_id : matched_record->name;
                        }
                    }

                    std::cout << "[DEBUG] 识别结果: face_id=" << (matched_face_id.empty() ? "empty" : matched_face_id)
                              << ", name=" << (person_name.empty() ? "empty" : person_name)
                              << ", match_score=" << match_score << std::endl;
                    
                    if (!person_name.empty()) {
                        cur_tracker.stable_face_id = matched_face_id;
                        cur_tracker.last_recognized_name = person_name;
                        cur_tracker.recognition_disabled = true;  // Stop recognition after successful match
                        cur_tracker.detection_attempts = 0;  // Reset detection attempts for known faces
                        std::cout << "[TRACK " << cur_tracker.id << "] recognized as: " << person_name << std::endl;
                        
                        // 🆕 发布人脸识别事件到消息总线（包含置信度）
                        uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count();
                        if (g_bus_bridge) {
                            json rec_payload;
                            rec_payload["event"] = "face_recognized";
                            rec_payload["id"] = cur_tracker.id;
                            rec_payload["face_id"] = matched_face_id;
                            rec_payload["name"] = person_name;
                            rec_payload["confidence"] = confidence;
                            rec_payload["match_score"] = match_score;
                            rec_payload["timestamp_ms"] = timestamp;
                            rec_payload["liveness"] = is_real_face;
                            rec_payload["metadata"] = {
                                {"match_score", match_score},
                                {"liveness_confidence", confidence}
                            };
                            std::cout << "[DEBUG] 📢 准备发布人脸识别事件: " << rec_payload.dump() << std::endl;
                            g_bus_bridge->publishRecognitionEvent(rec_payload);
                            std::cout << "[DEBUG] ✅ 人脸识别事件已发送: " << person_name << std::endl;
                        } else {
                            std::cout << "[DEBUG] ⚠️ g_bus_bridge 为 nullptr，无法发送人脸识别事件" << std::endl;
                        }
                        metrics.recognized_faces.fetch_add(1);
                    } else {
                        // Unknown person
                        cur_tracker.recognition_attempts++;
                        std::cout << "[TRACK " << cur_tracker.id << "] attempt " << cur_tracker.recognition_attempts
                                  << "/" << max_unknown_attempts << " - unknown" << std::endl;

                        if (!cur_tracker.prompted_for_registration && is_real_face) {
                            cur_tracker.detection_attempts++;
                            std::cout << "[TRACK " << cur_tracker.id << "] detection attempt " << cur_tracker.detection_attempts
                                      << "/" << detect_new_max_reco_times << " for new face registration" << std::endl;
                            
                            if (cur_tracker.detection_attempts >= detect_new_max_reco_times) {
                                cur_tracker.prompted_for_registration = true;

                                if (g_bus_bridge && g_bus_bridge->isFaceOpsAllowed()) {
                                    uint64_t timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::system_clock::now().time_since_epoch()).count();
                                    json new_face_payload;
                                    new_face_payload["event"] = "face_new";
                                    new_face_payload["tracker_id"] = cur_tracker.id;
                                    new_face_payload["id"] = cur_tracker.id;
                                    new_face_payload["detection_count"] = cur_tracker.detection_attempts;
                                    new_face_payload["bbox"] = {
                                        {"x1", large_box.x1 * ratio_x},
                                        {"y1", large_box.y1 * ratio_y},
                                        {"x2", large_box.x2 * ratio_x},
                                        {"y2", large_box.y2 * ratio_y}
                                    };
                                    new_face_payload["liveness"] = true;
                                    new_face_payload["confidence"] = confidence;
                                    new_face_payload["angle"] = angle;
                                    new_face_payload["timestamp_ms"] = timestamp;
                                    g_bus_bridge->publishEvent("event.vision.face.new", new_face_payload);
                                    std::cout << "[DEBUG] ✅ 新人脸事件已发送: " << new_face_payload.dump() << std::endl;
                                }

                                if (interactive_register_face) {
                                    std::string input_name;
                                    std::cout << "\n[REGISTER_FACE] 检测到新的人脸，请输入姓名并回车保存（留空跳过）: " << std::flush;
                                    if (!std::getline(std::cin, input_name)) {
                                        std::cin.clear();
                                    }
                                    input_name = trim_copy(input_name);

                                    if (input_name.empty()) {
                                        cur_tracker.recognition_disabled = true;
                                        person_name = "unknown";
                                        std::cout << "[REGISTER_FACE] 已跳过当前人脸注册" << std::endl;
                                    } else {
                                        std::string registered_face_id;
                                        if (register_face_record(
                                                cur_tracker,
                                                aligned_img,
                                                face_descriptor,
                                                large_box,
                                                confidence,
                                                angle,
                                                input_name,
                                                "interactive_cli",
                                                &registered_face_id)) {
                                            person_name = input_name;
                                            std::cout << "[REGISTER_FACE] 注册成功: " << input_name
                                                      << " (face_id=" << registered_face_id << ")" << std::endl;
                                        } else {
                                            cur_tracker.recognition_disabled = true;
                                            person_name = "unknown";
                                        }
                                    }
                                } else if (auto_register_new_face) {
                                    std::string registered_face_id;
                                    if (register_face_record(
                                            cur_tracker,
                                            aligned_img,
                                            face_descriptor,
                                            large_box,
                                            confidence,
                                            angle,
                                            "",
                                            "auto_new_face",
                                            &registered_face_id)) {
                                        person_name = registered_face_id;
                                    }
                                } else {
                                    cur_tracker.recognition_disabled = true;
                                    person_name = "unknown";
                                    std::cout << "[TRACK " << cur_tracker.id << "] stranger kept as unknown (auto-register disabled)" << std::endl;
                                }
                            }
                        } else if ((interactive_register_face || auto_register_new_face) && !is_real_face) {
                            // Detected fake face - skip registration attempts
                            std::cout << "[TRACK " << cur_tracker.id << "] detected as FAKE (confidence=" 
                                      << std::fixed << std::setprecision(3) << confidence 
                                      << "), skipping registration" << std::endl;
                        }

                        if (!cur_tracker.recognition_disabled && cur_tracker.recognition_attempts >= max_unknown_attempts) {
                            cur_tracker.recognition_disabled = true;
                            std::cout << "[TRACK " << cur_tracker.id << "] max unknown attempts reached, pausing" << std::endl;
                        }
                        
                        if (person_name.empty()) {
                            person_name = "unknown";
                        }
                    }
                    
                    cur_tracker.last_recognized_name = person_name;
                } else {
                    // Not recognizing in this frame (limited mode, already disabled)
                    liveface = (cur_tracker.last_recognized_name.empty() ? "tracking..." : cur_tracker.last_recognized_name);
                }
                
                // Determine box color based on recognition status
                cv::Scalar box_color;
                std::string status_prefix;
                
                if (should_recognize) {
                    // Recognition is active (green)
                    box_color = cv::Scalar(0, 255, 0);  // Green = recognition
                    status_prefix = "reco";
                } else if (!recognition_mode_enabled || cur_tracker.recognition_disabled || !enable_continuous_recognition) {
                    // Tracking only (yellow)
                    box_color = cv::Scalar(0, 255, 255);  // Yellow = tracking
                    status_prefix = "track";
                } else {
                    box_color = cv::Scalar(0, 255, 255);  // Yellow = tracking
                    status_prefix = "track";
                }
                
                // Change to red if unknown/failed recognition
                if (cur_tracker.last_recognized_name == "unknown" && !enable_continuous_recognition) {
                    box_color = cv::Scalar(0, 0, 255);  // Red = unknown/failed
                }
                
                // Draw bounding box
                int x1 = static_cast<int>(large_box.x1 * ratio_x);
                int y1 = static_cast<int>(large_box.y1 * ratio_y);
                int x2 = static_cast<int>(large_box.x2 * ratio_x);
                int y2 = static_cast<int>(large_box.y2 * ratio_y);
                int box_width = x2 - x1;
                int box_height = y2 - y1;
                cv::rectangle(frame, cv::Point(x1, y1), cv::Point(x2, y2), box_color, 2);
                
                // Adaptive font scale based on box size (smaller box = smaller font)
                // Configurable via config.ini: font_scale_base, font_scale_divisor, font_scale_cap, font_thickness_multiplier
                double font_scale_max = font_scale_base + (double)box_width / font_scale_divisor;
                double font_scale = std::min(font_scale_cap, font_scale_max);
                int thickness = std::max(1, (int)(font_scale * font_thickness_multiplier));
                
                // Debug logging for font scaling
                // static int log_counter = 0;
                // if (log_counter++ % 5 == 0) {  // Log every 30 frames to avoid spam
                //     std::cout << "[FONT DEBUG] box_width=" << box_width << ", box_height=" << box_height 
                //               << ", font_scale=" << std::fixed << std::setprecision(2) << font_scale 
                //               << ", font_scale_max" << font_scale_max
                //               << ", thickness=" << thickness << std::endl;
                // }
                
                // Determine liveness label
                std::string liveness_label = enable_liveness_detection ?
                    ((confidence <= true_thre) ? "fake" : "real") : "detected";
                
                // ==== TOP-LEFT: [track/reco]: name [confidence] ====
                std::ostringstream info_stream;
                info_stream << "[" << status_prefix << "]: " << cur_tracker.last_recognized_name << " ";
                // if (confidence > true_thre) {
                    // info_stream << std::fixed << std::setprecision(2) << (1.0 - confidence);
                // } else {
                    info_stream << std::fixed << std::setprecision(2) << confidence;
                // }
                std::string top_left_info = info_stream.str();
                
                // Draw top-left info inside box
                int baseline1 = 0;
                cv::Size text_size = cv::getTextSize(top_left_info, cv::FONT_HERSHEY_SIMPLEX, font_scale, thickness, &baseline1);
                // Ensure text stays within box bounds
                int text_y = std::min(y1 + text_size.height + 6, y2 - 5);
                cv::putText(frame, top_left_info, cv::Point(x1 + 4, text_y),
                           cv::FONT_HERSHEY_SIMPLEX, font_scale, cv::Scalar(0, 255, 0), thickness);  // Green text
                
                // ==== BOTTOM-LEFT: [fake/real] angle ====
                // Format: [fake/real] angle
                char angle_str[32];
                snprintf(angle_str, sizeof(angle_str), "[%s] %.1f", liveness_label.c_str(), angle);
                
                // Draw bottom-left info inside box
                int baseline2 = 0;
                cv::Size angle_size = cv::getTextSize(angle_str, cv::FONT_HERSHEY_SIMPLEX, font_scale, thickness, &baseline2);
                int angle_y = y2 - 6;  // Inside box, just above bottom edge
                
                // Color: red for "fake", green for "real"
                cv::Scalar text_color = (enable_liveness_detection && confidence <= true_thre) ?
                    cv::Scalar(0, 0, 255) : cv::Scalar(0, 255, 0);
                cv::putText(frame, angle_str, cv::Point(x1 + 4, angle_y),
                           cv::FONT_HERSHEY_SIMPLEX, font_scale, text_color, thickness);
            }
        }
        
        // ========== UPDATE TRACKER LOST STATE (EVERY FRAME) ==========
        std::vector<int> trackers_to_remove;
        for (auto& [tracker_id, tracker] : active_trackers) {
            if (tracker_matched.find(tracker_id) == tracker_matched.end()) {
                // This tracker was not matched in current frame
                tracker.lost_frames++;
                if (tracker.lost_frames >= 1) {
                    // std::cout << "[TRACK " << tracker_id << "] lost (no match, " 
                    //           << tracker.lost_frames << "/" << track_max_lost << " frames)" << std::endl;
                }
                
                if (tracker.lost_frames >= track_max_lost) {
                    std::cout << "[TRACKER REMOVED] id=" << tracker_id << " after " 
                              << tracker.lost_frames << " lost frames" << std::endl;
                    trackers_to_remove.push_back(tracker_id);
                }
            }
        }
        
        // Remove trackers that have been lost too long
        for (int tracker_id : trackers_to_remove) {
            // 🆕 发布人脸消失事件
            int lost_frames = active_trackers[tracker_id].lost_frames;
            if (g_bus_bridge) {
                json payload;
                payload["event"] = "face_lost";
                payload["id"] = tracker_id;
                payload["lost_frames"] = lost_frames;
                std::cout << "[DEBUG] 🔴 发送人脸消失事件: tracker_id=" << tracker_id 
                          << ", lost_frames=" << lost_frames << std::endl;
                g_bus_bridge->publishFaceLostEvent(payload);
                std::cout << "[DEBUG] ✅ 人脸消失事件已发送: " << payload.dump() << std::endl;
            } else {
                std::cout << "[DEBUG] ⚠️ g_bus_bridge 为 nullptr，无法发送人脸消失事件" << std::endl;
            }
            active_trackers.erase(tracker_id);
        }

        metrics.active_tracks.store(static_cast<int>(active_trackers.size()));
        } // end of if (should_detect)

        doly::vision::CaptureRequest capture_request;
        while (control.tryPopCaptureRequest(capture_request)) {
            uint64_t ts_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();

            auto draw_annotations = [&](cv::Mat& target) {
                for (const auto& face : faces_array) {
                    if (!face.contains("bbox")) {
                        continue;
                    }
                    auto bbox = face["bbox"];
                    int x1 = bbox.value("x1", 0);
                    int y1 = bbox.value("y1", 0);
                    int x2 = bbox.value("x2", 0);
                    int y2 = bbox.value("y2", 0);
                    cv::rectangle(target, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(0, 255, 0), 2);
                }
            };

            auto finish_video = [&](const std::string& status) {
                if (!video_recording) {
                    return;
                }
                video_writer.release();
                video_recording = false;

                uint64_t end_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();

                json payload;
                payload["request_id"] = video_request_id;
                payload["type"] = "video";
                payload["success"] = (status == "success");
                payload["status"] = status;
                payload["file_path"] = video_file_path;
                payload["duration_seconds"] = (end_ms - video_start_ms) / 1000.0;
                std::error_code ec;
                payload["file_size"] = std::filesystem::exists(video_file_path, ec)
                                          ? static_cast<int64_t>(std::filesystem::file_size(video_file_path, ec))
                                          : 0;
                bus.publishCaptureComplete(payload);
            };

            std::string req_type = capture_request.params.value("type", std::string("photo"));

            if (req_type == "video_start") {
                if (video_recording) {
                    json payload;
                    payload["request_id"] = capture_request.request_id;
                    payload["type"] = "video";
                    payload["status"] = "already_recording";
                    payload["start_time_ms"] = video_start_ms;
                    bus.publishCaptureStarted(payload);
                    continue;
                }

                std::string save_path = capture_request.params.value(
                    "save_path", Settings::getString("vision_video_path", project_path + "/videos"));
                std::filesystem::path dir(save_path);
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);

                std::string filename = capture_request.params.value("filename", std::string());
                std::string codec = capture_request.params.value("codec", std::string("mjpeg"));
                if (filename.empty()) {
                    filename = capture_request.request_id + "_" + std::to_string(ts_ms);
                }
                // 默认使用 MJPEG (AVI 格式)，因为 H264 与 MP4 格式不兼容
                if (codec == "mjpeg") {
                    if (filename.find('.') == std::string::npos) {
                        filename += ".avi";
                    }
                } else {
                    if (filename.find('.') == std::string::npos) {
                        filename += ".avi";  // 改为 .avi，避免 H264 与 MP4 不兼容的问题
                    }
                }

                video_file_path = (dir / filename).string();
                video_request_id = capture_request.request_id;
                video_include_annotations = capture_request.params.value("include_annotations", false);
                video_fps = capture_request.params.value("fps", cfg_f);
                video_max_duration_s = capture_request.params.value("max_duration_seconds", 0);
                video_start_ms = ts_ms;

                int fourcc = cv::VideoWriter::fourcc('M','J','P','G');

                video_writer.open(video_file_path, fourcc, video_fps,
                                  cv::Size(frame.cols, frame.rows));

                if (!video_writer.isOpened()) {
                    json payload;
                    payload["request_id"] = capture_request.request_id;
                    payload["type"] = "video";
                    payload["status"] = "open_failed";
                    payload["success"] = false;
                    bus.publishCaptureComplete(payload);
                    continue;
                }

                video_recording = true;
                json payload;
                payload["request_id"] = capture_request.request_id;
                payload["type"] = "video";
                payload["status"] = "recording";
                payload["start_time_ms"] = video_start_ms;
                bus.publishCaptureStarted(payload);
                continue;
            }

            if (req_type == "video_stop") {
                if (!video_recording) {
                    json payload;
                    payload["request_id"] = capture_request.request_id;
                    payload["type"] = "video";
                    payload["success"] = false;
                    payload["status"] = "not_recording";
                    bus.publishCaptureComplete(payload);
                    continue;
                }

                if (!capture_request.request_id.empty() &&
                    capture_request.request_id != video_request_id) {
                    json payload;
                    payload["request_id"] = capture_request.request_id;
                    payload["type"] = "video";
                    payload["success"] = false;
                    payload["status"] = "request_mismatch";
                    bus.publishCaptureComplete(payload);
                    continue;
                }

                finish_video("success");
                continue;
            }

            json capture_payload;
            capture_payload["request_id"] = capture_request.request_id;
            capture_payload["type"] = "photo";
            
            std::cerr << "[PhotoProc-1] 📸 开始处理拍照请求" << std::endl;
            std::cerr << "[PhotoProc-2] request_id=" << capture_request.request_id << std::endl;
            std::cerr << "[PhotoProc-3] has_primary_face=" << (has_primary_face ? "YES" : "NO") << std::endl;
            std::cout << "[DEBUG] 📸 处理拍照请求: request_id=" << capture_request.request_id 
                      << ", has_primary_face=" << has_primary_face << std::endl;

            // 检查是否需要人脸
            bool require_face = Settings::getBool("capture_require_face", false);
            std::cerr << "[PhotoProc-4] 配置 capture_require_face=" << (require_face ? "YES" : "NO") << std::endl;
            std::cout << "[DEBUG] 配置 capture_require_face=" << require_face << std::endl;
            
            if (require_face && !has_primary_face) {
                capture_payload["success"] = false;
                capture_payload["status"] = "no_face";
                std::cerr << "[PhotoProc-ERR] ❌ 需要人脸但未检测到" << std::endl;
                std::cout << "[DEBUG] ❌ 拍照失败: 需要人脸但未检测到" << std::endl;
                bus.publishCaptureComplete(capture_payload);
                continue;
            }

            std::cerr << "[PhotoProc-5] ✅ 人脸检查通过" << std::endl;
            bool include_annotations = capture_request.params.value("include_annotations", true);
            std::cerr << "[PhotoProc-6] include_annotations=" << (include_annotations ? "YES" : "NO") << std::endl;
            
            cv::Mat snapshot_frame = include_annotations ? frame.clone() : frame;
            if (include_annotations) {
                std::cerr << "[PhotoProc-7] 开始绘制标注" << std::endl;
                draw_annotations(snapshot_frame);
                std::cerr << "[PhotoProc-8] ✅ 标注绘制完成" << std::endl;
            }

            bool save_snapshot = capture_request.params.value("save_snapshot", true);
            std::cerr << "[PhotoProc-9] save_snapshot=" << (save_snapshot ? "YES" : "NO") << std::endl;
            
            if (save_snapshot) {
                std::string snapshot_dir = capture_request.params.value(
                    "save_path", Settings::getString("vision_photo_path", project_path + "/captures"));
                std::cerr << "[PhotoProc-10] 照片目录: " << snapshot_dir << std::endl;
                std::filesystem::path dir(snapshot_dir);
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                std::cerr << "[PhotoProc-11] ✅ 目录已创建" << std::endl;

                std::string filename = capture_request.params.value("filename", std::string());
                std::string format = capture_request.params.value("format", std::string("jpg"));
                if (filename.empty()) {
                    filename = capture_request.request_id + "_" + std::to_string(ts_ms) + "." + format;
                }
                std::cerr << "[PhotoProc-12] 文件名: " << filename << std::endl;

                std::filesystem::path file_path = dir / filename;
                std::vector<int> params;
                if (format == "jpg" || format == "jpeg") {
                    int quality = capture_request.params.value("quality", 95);
                    params.push_back(cv::IMWRITE_JPEG_QUALITY);
                    params.push_back(quality);
                    std::cerr << "[PhotoProc-13] JPEG质量: " << quality << std::endl;
                }
                
                std::cerr << "[PhotoProc-14] 开始保存照片: " << file_path.string() << std::endl;
                std::cout << "[DEBUG] 💾 保存照片: " << file_path.string() << std::endl;
                
                try {
                    if (!cv::imwrite(file_path.string(), snapshot_frame, params)) {
                        capture_payload["success"] = false;
                        capture_payload["status"] = "save_failed";
                        std::cerr << "[PhotoProc-ERR] ❌ cv::imwrite 返回 false" << std::endl;
                        std::cout << "[DEBUG] ❌ 照片保存失败: cv::imwrite 返回 false" << std::endl;
                    } else {
                        capture_payload["success"] = true;
                        capture_payload["file_path"] = file_path.string();
                        std::cout << "[DEBUG] ✅ 照片已保存: " << file_path.string() << std::endl;
                        capture_payload["file_size"] = std::filesystem::file_size(file_path, ec);
                        capture_payload["resolution"] = {
                            {"width", snapshot_frame.cols},
                            {"height", snapshot_frame.rows}
                        };
                        capture_payload["faces_detected"] = static_cast<int>(faces_array.size());
                        
                        // 拍照后显示照片功能
                        float display_duration = Settings::getFloat("capture_display_duration", 3.0f);
                        int display_lcd = Settings::getInt("capture_display_lcd", 1);
                        
                        if (display_duration > 0.0f) {
                            std::cout << "[DEBUG] 📷 显示照片到LCD " << display_lcd 
                                      << " 持续 " << display_duration << "秒" << std::endl;
                            
                            // 发送命令到eyeEngine显示图片
                            // TODO: 需要通过ZMQ发送命令
                            // 暂时只记录日志
                            std::cout << "[DEBUG] ✅ 照片路径: " << file_path.string() << std::endl;
                        }
                    }
                } catch (const std::exception& e) {
                    capture_payload["success"] = false;
                    capture_payload["status"] = "save_failed";
                    capture_payload["error"] = e.what();
                }
            } else {
                capture_payload["success"] = true;
            }

            bus.publishCaptureComplete(capture_payload);
        }

        if (video_recording) {
            if (video_include_annotations) {
                cv::Mat annotated = frame.clone();
                for (const auto& face : faces_array) {
                    if (!face.contains("bbox")) {
                        continue;
                    }
                    auto bbox = face["bbox"];
                    int x1 = bbox.value("x1", 0);
                    int y1 = bbox.value("y1", 0);
                    int x2 = bbox.value("x2", 0);
                    int y2 = bbox.value("y2", 0);
                    cv::rectangle(annotated, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(0, 255, 0), 2);
                }
                video_writer.write(annotated);
            } else {
                video_writer.write(frame);
            }

            if (video_max_duration_s > 0) {
                uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                if (now_ms - video_start_ms >= static_cast<uint64_t>(video_max_duration_s) * 1000) {
                    json payload;
                    payload["request_id"] = video_request_id;
                    payload["type"] = "video";
                    payload["status"] = "success";
                    video_writer.release();
                    video_recording = false;
                    payload["file_path"] = video_file_path;
                    payload["duration_seconds"] = (now_ms - video_start_ms) / 1000.0;
                    std::error_code ec;
                    payload["file_size"] = std::filesystem::exists(video_file_path, ec)
                                              ? static_cast<int64_t>(std::filesystem::file_size(video_file_path, ec))
                                              : 0;
                    bus.publishCaptureComplete(payload);
                }
            }
        }
        
        if (stream_publish_always || flag != 0) {
            cv::Mat publish_stream_frame;
            if (stream_enable_annotations) {
                if (output_width > 0 && output_height > 0 &&
                    (frame.cols != output_width || frame.rows != output_height)) {
                    cv::resize(frame, publish_stream_frame, cv::Size(output_width, output_height));
                } else {
                    publish_stream_frame = frame.clone();
                }
            } else {
                publish_stream_frame = stream_frame.clone();
            }
            PublishCurrentFrame(publish_stream_frame);
        }
        
        static AsyncSaver saver;
        if (file_save_count == 50) {
            file_save_count = 0;
            saver.saveImageAsync(project_path + "/temp.jpg", frame);
        } else {
            file_save_count++;
        }

        // Always show the current frame if GUI is enabled so the window is never black
        if (enable_gui) cv::imshow("LiveFaceReco", frame);

        // 🆕 只在检测到人脸时发布视频帧（由人脸检测/识别逻辑控制）
        // 具体在 detections.size() > 0 或识别成功时调用 PublishCurrentFrame(frame)

        if (enable_gui) {
            int k = cv::waitKey(33);
            if (k == 27) break;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(33));
        }
        count ++;
    }
    if (capture_active) {
        stop_camera_video(false);
    }
    return 0;
}