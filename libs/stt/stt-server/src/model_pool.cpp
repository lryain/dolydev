#include "model_pool.h"
#include "logger.h"
#include "server_config.h"
#include <chrono>
#include <exception>
#include <filesystem>

using namespace sherpa_onnx::cxx;

// VADModelPool 实现
VADModelPool::VADModelPool() {}

VADModelPool::~VADModelPool() {}

bool VADModelPool::initialize(const std::string &model_dir,
                              const ServerConfig &config) {
  std::lock_guard<std::mutex> lock(config_mutex);

  if (initialized.load()) {
    LOG_WARN("VAD_POOL", "VAD model pool already initialized");
    return true;
  }

  model_directory = model_dir;
  const auto &vad_config_params = config.get_vad_config();

  try {
    // 配置共享的VAD配置
    vad_config.silero_vad.model = model_dir + "/silero_vad/silero_vad.onnx";
    vad_config.silero_vad.threshold = vad_config_params.threshold;
    vad_config.silero_vad.min_silence_duration =
        vad_config_params.min_silence_duration;
    vad_config.silero_vad.min_speech_duration =
        vad_config_params.min_speech_duration;
    vad_config.silero_vad.max_speech_duration =
        vad_config_params.max_speech_duration;
    vad_config.silero_vad.window_size = vad_config_params.window_size;
    vad_config.sample_rate = vad_config_params.sample_rate;
    vad_config.debug = vad_config_params.debug;

    // 测试VAD配置是否有效 (buffer_size_in_seconds)
    auto test_vad = VoiceActivityDetector::Create(vad_config, 30.0f);
    if (!test_vad.Get()) {
      LOG_ERROR("VAD_POOL", "Failed to validate VAD configuration");
      return false;
    }

    LOG_INFO("VAD_POOL", "VAD pool initialized successfully");
    initialized = true;
    return true;

  } catch (const std::exception &e) {
    LOG_ERROR("VAD_POOL", "Error initializing VAD pool: " << e.what());
    return false;
  }
}

std::unique_ptr<VoiceActivityDetector>
VADModelPool::create_vad_instance() const {
  std::lock_guard<std::mutex> lock(config_mutex);

  if (!initialized.load()) {
    LOG_ERROR("VAD_POOL", "VAD pool not initialized");
    return nullptr;
  }

  try {
    // 为每个会话创建独立的VAD实例，但共享模型文件
    auto vad_obj = VoiceActivityDetector::Create(vad_config, 30.0f);
    if (!vad_obj.Get()) {
      LOG_ERROR("VAD_POOL", "Failed to create VAD instance");
      return nullptr;
    }

    return std::make_unique<VoiceActivityDetector>(std::move(vad_obj));

  } catch (const std::exception &e) {
    LOG_ERROR("VAD_POOL", "Error creating VAD instance: " << e.what());
    return nullptr;
  }
}

float VADModelPool::get_sample_rate() const { return sample_rate; }

bool VADModelPool::is_initialized() const { return initialized.load(); }

// ModelManager 实现 - 向后兼容的接口
ModelManager::ModelManager([[maybe_unused]] int asr_pool_size) {
  // asr_pool_size 参数保留但不使用，因为现在使用共享ASR
  shared_asr = std::make_unique<SharedASREngine>();
  vad_pool = std::make_unique<VADModelPool>();
}

ModelManager::~ModelManager() {}

bool ModelManager::initialize(const std::string &model_dir,
                              const ServerConfig &config) {
  LOG_INFO("MODEL_MANAGER",
           "Initializing legacy model manager (using shared ASR)");

  // 初始化共享ASR引擎
  if (!shared_asr->initialize(model_dir, config)) {
    LOG_ERROR("MODEL_MANAGER", "Failed to initialize shared ASR engine");
    return false;
  }

  // 初始化VAD池
  if (!vad_pool->initialize(model_dir, config)) {
    LOG_ERROR("MODEL_MANAGER", "Failed to initialize VAD pool");
    return false;
  }

  initialized = true;
  LOG_INFO("MODEL_MANAGER", "Legacy model manager initialized successfully");
  return true;
}

int ModelManager::acquire_asr_recognizer([[maybe_unused]] int timeout_ms) {
  if (!initialized.load()) {
    LOG_ERROR("MODEL_MANAGER", "Model manager not initialized");
    return -1;
  }

  // 在共享模式下，总是返回固定ID 0，表示可以使用共享引擎
  if (shared_asr && shared_asr->is_initialized()) {
    LOG_DEBUG("MODEL_MANAGER", "Acquired shared ASR engine (ID: 0)");
    return 0;
  }

  LOG_ERROR("MODEL_MANAGER", "Shared ASR engine not available");
  return -1;
}

void ModelManager::release_asr_recognizer(int instance_id) {
  // 在共享模式下，释放操作是空操作
  LOG_DEBUG("MODEL_MANAGER",
            "Released shared ASR engine (ID: " << instance_id << ")");
}

std::unique_ptr<VoiceActivityDetector>
ModelManager::create_vad_instance() const {
  if (!initialized.load()) {
    LOG_ERROR("MODEL_MANAGER", "Model manager not initialized");
    return nullptr;
  }

  return vad_pool->create_vad_instance();
}

float ModelManager::get_sample_rate() const {
  if (shared_asr) {
    return shared_asr->get_sample_rate();
  }
  return 16000; // 默认值
}

bool ModelManager::is_initialized() const { return initialized.load(); }

ModelManager::LegacyStats ModelManager::get_asr_pool_stats() const {
  LegacyStats stats;
  if (initialized.load()) {
    // 共享ASR模式下的统计信息
    stats.total_instances = 1;
    stats.available_instances = 1;
    stats.in_use_instances =
        shared_asr ? shared_asr->get_active_recognitions() : 0;
  }

  return stats;
}

// SharedASREngine 实现 - 共享ASR引擎，支持多客户端并发访问

// 检测模型类型：根据模型目录中的文件名判断
SharedASREngine::ModelType SharedASREngine::detect_model_type(
    const std::string &model_dir) {
  namespace fs = std::filesystem;
  
  // 检查 encoder 文件是否存在（Zipformer特征）
  for (const auto &entry : fs::directory_iterator(model_dir)) {
    if (entry.is_regular_file()) {
      const auto &filename = entry.path().filename().string();
      // 检查是否包含 "encoder" 字符串
      if (filename.find("encoder") != std::string::npos && 
          filename.find(".onnx") != std::string::npos) {
        LOG_INFO("SHARED_ASR", "Detected Zipformer model from encoder file: " 
                                   << filename);
        return ModelType::ZIPFORMER;
      }
    }
  }
  
  // 检查 model.onnx 文件是否存在（SenseVoice特征）
  if (fs::exists(fs::path(model_dir) / "model.onnx")) {
    LOG_INFO("SHARED_ASR", "Detected SenseVoice model from model.onnx");
    return ModelType::SENSE_VOICE;
  }
  
  LOG_WARN("SHARED_ASR", "Could not detect model type from: " << model_dir);
  return ModelType::UNKNOWN;
}

// 初始化 SenseVoice 离线识别器
bool SharedASREngine::initialize_sense_voice(const std::string &model_dir,
                                              const ServerConfig &config) {
  const auto &asr_config = config.get_asr_config();
  
  try {
    OfflineRecognizerConfig recognizer_config;
    recognizer_config.model_config.sense_voice.model =
        model_dir + "/model.onnx";
    recognizer_config.model_config.sense_voice.use_itn = asr_config.use_itn;
    recognizer_config.model_config.sense_voice.language = asr_config.language;
    recognizer_config.model_config.tokens = model_dir + "/tokens.txt";
    recognizer_config.model_config.num_threads = asr_config.num_threads;
    recognizer_config.model_config.debug = asr_config.debug;

    LOG_INFO("SHARED_ASR", "Initializing SenseVoice model from: " << model_dir);

    auto recognizer_obj = OfflineRecognizer::Create(recognizer_config);
    if (!recognizer_obj.Get()) {
      LOG_ERROR("SHARED_ASR", "Failed to create SenseVoice offline recognizer");
      return false;
    }

    offline_recognizer = 
        std::make_unique<OfflineRecognizer>(std::move(recognizer_obj));
    model_type = ModelType::SENSE_VOICE;
    sample_rate = 16000;

    LOG_INFO("SHARED_ASR", "SenseVoice model initialized successfully");
    return true;

  } catch (const std::exception &e) {
    LOG_ERROR("SHARED_ASR", "Error initializing SenseVoice: " << e.what());
    return false;
  }
}

// 初始化 Streaming Zipformer 在线识别器
bool SharedASREngine::initialize_zipformer(const std::string &model_dir,
                                            const ServerConfig &config) {
  const auto &asr_config = config.get_asr_config();
  
  try {
    // 获取编码器、解码器、连接器路径  
    namespace fs = std::filesystem;
    std::string encoder_path, decoder_path, joiner_path;
    
    // 如果配置中指定了路径，使用配置的路径；否则自动查找
    if (!asr_config.encoder_path.empty()) {
      encoder_path = asr_config.encoder_path;
      decoder_path = asr_config.decoder_path;
      joiner_path = asr_config.joiner_path;
    } else {
      // 自动查找 encoder、decoder、joiner 文件
      for (const auto &entry : fs::directory_iterator(model_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".onnx") {
          const auto &filename = entry.path().filename().string();
          if (filename.find("encoder") != std::string::npos) {
            encoder_path = entry.path().string();
          } else if (filename.find("decoder") != std::string::npos) {
            decoder_path = entry.path().string();
          } else if (filename.find("joiner") != std::string::npos) {
            joiner_path = entry.path().string();
          }
        }
      }
    }
    
    if (encoder_path.empty() || decoder_path.empty() || joiner_path.empty()) {
      LOG_ERROR("SHARED_ASR", "Missing Zipformer model files in: " << model_dir);
      LOG_ERROR("SHARED_ASR", "  encoder: " << encoder_path);
      LOG_ERROR("SHARED_ASR", "  decoder: " << decoder_path);
      LOG_ERROR("SHARED_ASR", "  joiner: " << joiner_path);
      return false;
    }

    OnlineRecognizerConfig recognizer_config;
    recognizer_config.model_config.transducer.encoder = encoder_path;
    recognizer_config.model_config.transducer.decoder = decoder_path;
    recognizer_config.model_config.transducer.joiner = joiner_path;
    recognizer_config.model_config.tokens = model_dir + "/tokens.txt";
    recognizer_config.model_config.num_threads = asr_config.num_threads;
    recognizer_config.model_config.debug = asr_config.debug;

    LOG_INFO("SHARED_ASR", "Initializing Zipformer model");
    LOG_INFO("SHARED_ASR", "  encoder: " << encoder_path);
    LOG_INFO("SHARED_ASR", "  decoder: " << decoder_path);
    LOG_INFO("SHARED_ASR", "  joiner: " << joiner_path);

    auto recognizer_obj = OnlineRecognizer::Create(recognizer_config);
    if (!recognizer_obj.Get()) {
      LOG_ERROR("SHARED_ASR", "Failed to create Zipformer online recognizer");
      return false;
    }

    online_recognizer = 
        std::make_unique<OnlineRecognizer>(std::move(recognizer_obj));
    model_type = ModelType::ZIPFORMER;
    sample_rate = 16000;

    LOG_INFO("SHARED_ASR", "Zipformer model initialized successfully");
    return true;

  } catch (const std::exception &e) {
    LOG_ERROR("SHARED_ASR", "Error initializing Zipformer: " << e.what());
    return false;
  }
}

// SharedASREngine 实现 - 共享ASR引擎，支持多客户端并发访问
SharedASREngine::SharedASREngine() {}

SharedASREngine::~SharedASREngine() {}

bool SharedASREngine::initialize(const std::string &model_dir,
                                 const ServerConfig &config) {
  std::lock_guard<std::mutex> lock(engine_mutex);

  if (initialized.load()) {
    LOG_WARN("SHARED_ASR", "Shared ASR engine already initialized");
    return true;
  }

  const auto &asr_config = config.get_asr_config();
  
  // 组合模型目录和模型名称
  namespace fs = std::filesystem;
  std::string full_model_path = model_dir;
  
  // 如果 model_dir 不包含模型名称，则组合它
  if (!model_dir.empty() && !asr_config.model_name.empty()) {
    // 检查 model_dir 是否已经包含模型名称（通过检查是否存在ONNX文件）
    bool has_model_files = false;
    try {
      for (const auto &entry : fs::directory_iterator(model_dir)) {
        if (entry.path().extension() == ".onnx") {
          has_model_files = true;
          break;
        }
      }
    } catch (...) {
      has_model_files = false;
    }
    
    // 如果model_dir中没有模型文件，说明需要与model_name组合
    if (!has_model_files) {
      full_model_path = fs::path(model_dir) / asr_config.model_name;
      LOG_INFO("SHARED_ASR", "Combining model path: " << model_dir 
                                 << " + " << asr_config.model_name 
                                 << " = " << full_model_path);
    }
  }

  model_directory = full_model_path;

  try {
    // 确定模型类型
    ModelType detected_type = ModelType::UNKNOWN;
    
    if (asr_config.model_type == "auto") {
      detected_type = detect_model_type(full_model_path);
    } else if (asr_config.model_type == "sense-voice") {
      detected_type = ModelType::SENSE_VOICE;
    } else if (asr_config.model_type == "zipformer") {
      detected_type = ModelType::ZIPFORMER;
    } else {
      LOG_WARN("SHARED_ASR", "Unknown model_type: " << asr_config.model_type 
                                 << ", attempting auto-detection");
      detected_type = detect_model_type(full_model_path);
    }

    if (detected_type == ModelType::SENSE_VOICE) {
      LOG_INFO("SHARED_ASR", "Using SenseVoice model");
      if (!initialize_sense_voice(full_model_path, config)) {
        return false;
      }
    } else if (detected_type == ModelType::ZIPFORMER) {
      LOG_INFO("SHARED_ASR", "Using Zipformer model");
      if (!initialize_zipformer(full_model_path, config)) {
        return false;
      }
    } else {
      LOG_ERROR("SHARED_ASR", "Failed to detect or initialize model");
      return false;
    }

    LOG_INFO("SHARED_ASR", "Shared ASR engine initialized successfully "
                               << "with " << asr_config.num_threads << " threads");
    initialized = true;
    return true;

  } catch (const std::exception &e) {
    LOG_ERROR("SHARED_ASR",
              "Error initializing shared ASR engine: " << e.what());
    return false;
  }
}

std::string SharedASREngine::recognize(const float *samples,
                                       size_t sample_count) {
  if (!initialized.load()) {
    LOG_ERROR("SHARED_ASR", "Shared ASR engine not initialized");
    return "";
  }

  // 线程安全的识别
  std::lock_guard<std::mutex> lock(engine_mutex);
  active_recognitions++;

  try {
    std::string result_text;

    if (model_type == ModelType::SENSE_VOICE) {
      // SenseVoice 离线模式
      OfflineStream stream = offline_recognizer->CreateStream();
      stream.AcceptWaveform(sample_rate, samples, sample_count);
      offline_recognizer->Decode(&stream);

      OfflineRecognizerResult result = offline_recognizer->GetResult(&stream);
      result_text = result.text;

    } else if (model_type == ModelType::ZIPFORMER) {
      // Zipformer 在线模式
      auto stream = online_recognizer->CreateStream();
      stream.AcceptWaveform(sample_rate, samples, sample_count);
      
      // 对于在线模型，需要反复调用 Decode 直到完成
      while (online_recognizer->IsReady(&stream)) {
        online_recognizer->Decode(&stream);
      }
      
      // 最后填充静音以完成识别
      stream.InputFinished();
      while (online_recognizer->IsReady(&stream)) {
        online_recognizer->Decode(&stream);
      }

      auto result = online_recognizer->GetResult(&stream);
      result_text = result.text;

    } else {
      LOG_ERROR("SHARED_ASR", "Unknown model type");
      active_recognitions--;
      return "";
    }

    active_recognitions--;
    return result_text;

  } catch (const std::exception &e) {
    active_recognitions--;
    LOG_ERROR("SHARED_ASR", "Error in recognition: " << e.what());
    return "";
  }
}

std::string SharedASREngine::recognize_with_metadata(
    const float *samples, size_t sample_count, std::string &language,
    std::string &emotion, std::string &event, std::vector<float> &timestamps,
    std::vector<std::string> &tokens) {
  if (!initialized.load()) {
    LOG_ERROR("SHARED_ASR", "Shared ASR engine not initialized");
    return "";
  }

  // 线程安全的识别
  std::lock_guard<std::mutex> lock(engine_mutex);
  active_recognitions++;

  try {
    std::string result_text;

    if (model_type == ModelType::SENSE_VOICE) {
      // SenseVoice 离线模式 - 支持元数据
      OfflineStream stream = offline_recognizer->CreateStream();
      stream.AcceptWaveform(sample_rate, samples, sample_count);
      offline_recognizer->Decode(&stream);

      OfflineRecognizerResult result = offline_recognizer->GetResult(&stream);
      result_text = result.text;
      language = result.lang;
      emotion = result.emotion;
      event = result.event;
      timestamps = result.timestamps;
      tokens = result.tokens;

    } else if (model_type == ModelType::ZIPFORMER) {
      // Zipformer 在线模式 - 不支持元数据，返回空值
      auto stream = online_recognizer->CreateStream();
      stream.AcceptWaveform(sample_rate, samples, sample_count);
      
      while (online_recognizer->IsReady(&stream)) {
        online_recognizer->Decode(&stream);
      }
      
      stream.InputFinished();
      while (online_recognizer->IsReady(&stream)) {
        online_recognizer->Decode(&stream);
      }

      auto result = online_recognizer->GetResult(&stream);
      result_text = result.text;
      
      // Zipformer 不提供这些元数据，清空它们
      language = "";
      emotion = "";
      event = "";
      timestamps.clear();
      tokens.clear();
      
      LOG_DEBUG("SHARED_ASR", "Note: Zipformer model does not provide "
                                  "language/emotion/event metadata");

    } else {
      LOG_ERROR("SHARED_ASR", "Unknown model type");
      active_recognitions--;
      return "";
    }

    active_recognitions--;
    return result_text;

  } catch (const std::exception &e) {
    active_recognitions--;
    LOG_ERROR("SHARED_ASR", "Error in recognition with metadata: " << e.what());
    return "";
  }
}

// VADPool 实现 - 动态VAD池管理
VADPool::VADPool(const std::string &model_dir, const ServerConfig &config)
    : model_directory(model_dir), max_instances(20), min_instances(2) {

  const auto &vad_config_params = config.get_vad_config();

  // 配置VAD
  vad_config.silero_vad.model = model_dir + "/silero_vad/silero_vad.onnx";
  vad_config.silero_vad.threshold = vad_config_params.threshold;
  vad_config.silero_vad.min_silence_duration =
      vad_config_params.min_silence_duration;
  vad_config.silero_vad.min_speech_duration =
      vad_config_params.min_speech_duration;
  vad_config.silero_vad.max_speech_duration =
      vad_config_params.max_speech_duration;
  vad_config.silero_vad.window_size = vad_config_params.window_size;
  vad_config.sample_rate = vad_config_params.sample_rate;
  sample_rate = vad_config_params.sample_rate;
}

VADPool::~VADPool() {
  std::lock_guard<std::mutex> lock(pool_mutex);
  while (!vad_pool.empty()) {
    vad_pool.pop();
  }
}

std::unique_ptr<VoiceActivityDetector> VADPool::create_vad_instance() {
  try {
    auto vad_obj = VoiceActivityDetector::Create(
        vad_config, 30.0f); // buffer_size_in_seconds
    if (!vad_obj.Get()) {
      LOG_ERROR("VAD_POOL", "Failed to create VAD instance");
      return nullptr;
    }

    return std::make_unique<VoiceActivityDetector>(std::move(vad_obj));

  } catch (const std::exception &e) {
    LOG_ERROR("VAD_POOL", "Error creating VAD instance: " << e.what());
    return nullptr;
  }
}

std::unique_ptr<VoiceActivityDetector>
VADPool::acquire(std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(pool_mutex);

  // 如果池中有可用实例，直接返回
  if (!vad_pool.empty()) {
    auto vad = std::move(vad_pool.front());
    vad_pool.pop();
    available_instances--;
    LOG_DEBUG("VAD_POOL", "Acquired VAD from pool, available: "
                              << available_instances.load());
    return vad;
  }

  // 如果没达到最大实例数，创建新实例
  if (total_instances.load() < max_instances) {
    lock.unlock();
    auto vad = create_vad_instance();
    if (vad) {
      total_instances++;
      LOG_DEBUG("VAD_POOL",
                "Created new VAD instance, total: " << total_instances.load());
      return vad;
    }
    lock.lock();
  }

  // 等待可用实例
  if (pool_cv.wait_for(lock, timeout, [this] { return !vad_pool.empty(); })) {
    auto vad = std::move(vad_pool.front());
    vad_pool.pop();
    available_instances--;
    LOG_DEBUG("VAD_POOL", "Acquired VAD after wait, available: "
                              << available_instances.load());
    return vad;
  }

  LOG_WARN("VAD_POOL", "Timeout waiting for available VAD instance");
  return nullptr;
}

void VADPool::release(std::unique_ptr<VoiceActivityDetector> vad) {
  if (!vad)
    return;

  std::lock_guard<std::mutex> lock(pool_mutex);

  // 如果池满了，直接丢弃实例
  if (vad_pool.size() >= max_instances) {
    total_instances--;
    LOG_DEBUG("VAD_POOL", "Pool full, discarding VAD instance, total: "
                              << total_instances.load());
    return;
  }

  vad_pool.push(std::move(vad));
  available_instances++;
  LOG_DEBUG("VAD_POOL",
            "Released VAD to pool, available: " << available_instances.load());

  pool_cv.notify_one();
}

bool VADPool::initialize() {
  // 预热池 - 创建最小数量的实例
  for (size_t i = 0; i < min_instances; ++i) {
    auto vad = create_vad_instance();
    if (!vad) {
      LOG_ERROR("VAD_POOL", "Failed to create initial VAD instance " << i);
      return false;
    }

    std::lock_guard<std::mutex> lock(pool_mutex);
    vad_pool.push(std::move(vad));
    total_instances++;
    available_instances++;
  }

  LOG_INFO("VAD_POOL",
           "VAD pool initialized with " << min_instances << " instances");
  return true;
}

// ModelPoolManager 实现 - 统一管理ASR和VAD资源
ModelPoolManager::ModelPoolManager() {
  asr_engine = std::make_unique<SharedASREngine>();
}

ModelPoolManager::~ModelPoolManager() {}

bool ModelPoolManager::initialize(const std::string &model_dir,
                                  const ServerConfig &config) {
  LOG_INFO("MODEL_POOL_MANAGER", "Initializing model pool manager");

  // 初始化共享ASR引擎
  if (!asr_engine->initialize(model_dir, config)) {
    LOG_ERROR("MODEL_POOL_MANAGER", "Failed to initialize shared ASR engine");
    return false;
  }

  // 初始化VAD池
  vad_pool = std::make_unique<VADPool>(model_dir, config);
  if (!vad_pool->initialize()) {
    LOG_ERROR("MODEL_POOL_MANAGER", "Failed to initialize VAD pool");
    return false;
  }

  LOG_INFO("MODEL_POOL_MANAGER", "Model pool manager initialized successfully");
  return true;
}

void ModelPoolManager::session_started() {
  auto current = total_sessions.fetch_add(1) + 1;

  // 更新峰值并发会话数
  auto peak = peak_concurrent_sessions.load();
  while (current > peak &&
         !peak_concurrent_sessions.compare_exchange_weak(peak, current)) {
    peak = peak_concurrent_sessions.load();
  }

  LOG_INFO("MODEL_POOL_MANAGER",
           "Session started, current active: " << current);
}

void ModelPoolManager::session_ended() {
  auto current = total_sessions.fetch_sub(1) - 1;
  LOG_INFO("MODEL_POOL_MANAGER", "Session ended, current active: " << current);
}

ModelPoolManager::SystemStats ModelPoolManager::get_system_stats() const {
  std::lock_guard<std::mutex> lock(stats_mutex);

  SystemStats stats;
  stats.total_sessions = total_sessions.load();
  stats.peak_concurrent_sessions = peak_concurrent_sessions.load();
  stats.current_active_sessions = total_sessions.load();
  stats.asr_active_recognitions = asr_engine->get_active_recognitions();
  stats.vad_total_instances = vad_pool->get_total_instances();
  stats.vad_available_instances = vad_pool->get_available_instances();
  stats.vad_active_instances = vad_pool->get_active_instances();

  if (stats.vad_total_instances > 0) {
    stats.memory_efficiency_ratio =
        static_cast<float>(stats.vad_active_instances) /
        stats.vad_total_instances;
  } else {
    stats.memory_efficiency_ratio = 0.0f;
  }

  return stats;
}

void ModelPoolManager::log_system_stats() const {
  auto stats = get_system_stats();
  LOG_INFO("MODEL_POOL_MANAGER",
           "System stats - Active sessions: "
               << stats.current_active_sessions
               << ", Peak sessions: " << stats.peak_concurrent_sessions
               << ", ASR recognitions: " << stats.asr_active_recognitions
               << ", VAD instances (total/available/active): "
               << stats.vad_total_instances << "/"
               << stats.vad_available_instances << "/"
               << stats.vad_active_instances << ", Memory efficiency: "
               << (stats.memory_efficiency_ratio * 100) << "%");
}
