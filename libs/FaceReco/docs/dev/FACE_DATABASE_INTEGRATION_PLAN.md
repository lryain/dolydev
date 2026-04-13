# FaceDatabase 集成完整方案

**日期**: 2026-02-11  
**目标**: 启用 FaceDatabase，实现完整的人脸数据管理

---

## 🎯 架构设计（正确方案）

### 当前架构分析

```
┌─────────────────────────────────────────────────┐
│              Vision Service                      │
├─────────────────────────────────────────────────┤
│  ✅ FaceDatabase (face_db.json)                 │  主数据库
│      - 姓名、关系、元数据                        │
│      - 注册时间、最后出现                        │
│      - 照片路径、样本数量                        │
│                                                  │
│  ✅ descriptors.yml                             │  性能缓存
│      - 仅存特征向量                              │
│      - 加速启动和识别                            │
│                                                  │
│  ✅ Arcface 特征提取                            │  算法模块
│      - 提取 512 维特征向量                       │
└─────────────────────────────────────────────────┘
```

### 数据流设计

```
1. 注册新人脸:
   Daemon (ZMQ cmd) 
     → Vision Service handleFaceCommand()
       → FaceDatabase.addOrUpdate()
       → face_db.json (持久化)
       → descriptors.yml (更新缓存)
       → 发布 event.vision.face.registered

2. 识别人脸:
   Camera Frame
     → Arcface 提取特征
     → 与 descriptors.yml 比对 (快速)
     → 匹配成功 → 从 face_db.json 读取元数据
     → 发布 event.vision.face.recognized

3. 更新/删除:
   Daemon (ZMQ cmd)
     → Vision Service handleFaceCommand()
       → FaceDatabase.update() / remove()
       → face_db.json (持久化)
       → descriptors.yml (同步更新)
```

---

## ✅ 现状确认

### 已实现的功能

1. ✅ **FaceDatabase 类完整实现**
   - 文件：`src/face_database.cpp`
   - API: add, update, delete, get, list
   - 存储：JSON 格式

2. ✅ **ZMQ 命令处理已实现**
   - `vision_service.cpp::handleFaceCommand()`
   - 支持：register, update, delete, recapture
   - 已连接到 FaceDatabase API

3. ✅ **VisionBusBridge 集成**
   - FaceDatabase 已传递给 bridge
   - 命令路由正常工作

### 问题分析

#### 问题：descriptors.yml 和 face_db.json 未联动

**当前行为**:
```cpp
// livefacereco.cpp L692-695
bool enable_descriptor_cache = Settings::getBool("enable_descriptor_cache", true);
std::string descriptor_cache_path = Settings::getString("descriptor_cache_path", 
    project_path + "/descriptors.yml");

// ❌ 直接从 descriptors.yml 加载，忽略 face_db.json
```

**预期行为**:
```cpp
1. 启动时从 face_db.json 加载元数据
2. 如果 enable_descriptor_cache=true:
   - 尝试加载 descriptors.yml
   - 检查缓存是否与 face_db.json 一致
   - 不一致则重新生成缓存
3. 注册/更新/删除时同步更新两个文件
```

---

## 🔧 实施方案

### 方案 A: 双存储协同（推荐）

**优点**:
- 保留性能优化
- 完整元数据管理
- 最小代码改动

**实施步骤**:

1. **修改 livefacereco.cpp 初始化逻辑** (30分钟)
   ```cpp
   // 步骤 1: 从 face_db.json 加载
   FaceDatabase face_db(project_path + "/data/face_db.json");
   face_db.load();
   auto all_faces = face_db.list();
   
   // 步骤 2: 生成/更新 descriptors.yml
   if (enable_descriptor_cache) {
       syncDescriptorCache(all_faces, descriptor_cache_path);
   }
   
   // 步骤 3: 加载到 Arcface
   for (const auto& face : all_faces) {
       arcface->loadDescriptor(face.face_id, face.name);
   }
   ```

2. **添加缓存同步函数** (20分钟)
   ```cpp
   void syncDescriptorCache(
       const std::vector<FaceRecord>& faces,
       const std::string& cache_path
   ) {
       // 读取现有缓存
       cv::FileStorage fs_read(cache_path, cv::FileStorage::READ);
       std::unordered_set<std::string> cached_ids;
       // ... 检查哪些需要更新
       
       // 重新写入
       cv::FileStorage fs_write(cache_path, cv::FileStorage::WRITE);
       for (const auto& face : faces) {
           fs_write << face.face_id << face.descriptor;
       }
   }
   ```

3. **修改注册流程** (15分钟)
   ```cpp
   // 当前：直接写 descriptors.yml
   // 修改为：
   //   1. 调用 FaceDatabase::addOrUpdate()
   //   2. FaceDatabase::save() → face_db.json
   //   3. 更新 descriptors.yml 缓存
   //   4. 通知 Arcface 重新加载
   ```

### 方案 B: 仅用 FaceDatabase（简单但性能差）

**优点**:
- 架构简单
- 无缓存一致性问题

**缺点**:
- 每次启动都要重新提取特征（慢）
- 识别时需要实时计算（延迟高）

**不推荐原因**: 违背了 descriptors.yml 的设计初衷

---

## 📋 详细实施清单（方案 A）

### Phase 0: 配置和状态管理 (✅ 部分完成)

**问题 #1: IDLE 模式未生效**
- [x] 添加状态切换调试日志（vision_service.cpp）
- [ ] 验证 default_mode: IDLE 配置加载（需要测试）
- [ ] 监听 cmd_ActWhoami (0x07) 命令
- [ ] 实现超时自动恢复到 IDLE（可配置超时时间）
- [ ] 添加状态转换日志：IDLE → FULL → IDLE

**问题 #2: 人脸跟踪功能**
- [x] 验证 face_tracking_control 配置存在（配置文件已完善）
- [ ] 实现眼睛跟踪模式（发送 gaze 命令到 eyeEngine）
- [ ] 实现电机跟踪模式（发送 motor 命令保持人脸居中）
- [ ] 支持跟踪模式切换：gaze_only, motor_only, both, disabled
- [ ] 添加跟踪状态调试日志

**问题 #3: set_gaze 命令报错**
- [x] 添加 set_gaze 命令到 eyeEngine（zmq_service.py）
- [x] 实现 _cmd_set_gaze 函数
- [ ] 测试眼睛跟踪命令发送
- [ ] 验证命令执行效果

### Phase 1: 基础集成 (已完成 ✅)

- [x] FaceDatabase 类实现
- [x] ZMQ 命令处理
- [x] JSON 存储格式

### Phase 2: 缓存同步 (待实施 ⏳)

1. **创建同步工具类** `FaceDescriptorCache`
   ```cpp
   class FaceDescriptorCache {
   public:
       bool load(const std::string& path);
       bool save(const std::string& path);
       void addOrUpdate(const std::string& id, const cv::Mat& descriptor);
       void remove(const std::string& id);
       cv::Mat get(const std::string& id);
   private:
       std::unordered_map<std::string, cv::Mat> cache_;
   };
   ```

2. **修改 livefacereco.cpp 初始化**
   - 位置：`MTCNNDetection()` 函数开始
   - 改动：先加载 face_db.json，再同步缓存

3. **修改注册/更新/删除流程**
   - 位置：handleFaceCommand() 回调处理
   - 改动：操作后同步更新缓存

### Phase 3: 测试验证 (⏳)

1. **单元测试**
   ```cpp
   // 测试缓存同步
   TEST(FaceDatabase, CacheSyncAfterAdd) {
       FaceDatabase db("test_db.json");
       FaceDescriptorCache cache;
       
       // 添加人脸
       FaceRecord record;
       record.face_id = "test123";
       record.name = "张三";
       db.addOrUpdate(record);
       
       // 同步缓存
       syncCache(db, cache);
       
       // 验证
       ASSERT_TRUE(cache.has("test123"));
   }
   ```

2. **集成测试**
   ```bash
   # 1. 清空数据
   rm -f data/face_db.json descriptors.yml
   
   # 2. 启动服务
   ./LiveFaceReco
   
   # 3. 注册人脸
   python test_register_face.py --name 张三
   
   # 4. 验证文件
   ls -lh data/face_db.json   # 应该存在
   ls -lh descriptors.yml      # 应该存在
   cat data/face_db.json | jq  # 应该有张三的记录
   ```

---

## 🚀 快速启用指南

### 最小改动方案（30分钟）

如果时间紧迫，可以先做这个最小改动：

**文件**: `livefacereco.cpp`

**位置**: `MTCNNDetection()` 开始处

**改动**:
```cpp
// 原代码（L690-700）:
bool enable_descriptor_cache = Settings::getBool("enable_descriptor_cache", true);
std::string descriptor_cache_path = Settings::getString("descriptor_cache_path", 
    project_path + "/descriptors.yml");

// 新增代码:
// ✅ 从 FaceDatabase 加载元数据
FaceDatabase face_db(project_path + "/data/face_db.json");
if (face_db.load()) {
    std::cout << "[FaceDB] ✅ 加载了 " << face_db.list().size() << " 条人脸记录" << std::endl;
} else {
    std::cout << "[FaceDB] ⚠️ face_db.json 不存在或为空，将创建新数据库" << std::endl;
}

// 保留原有的 descriptor cache 加载逻辑
if (enable_descriptor_cache) {
    // ... 现有代码
}
```

**效果**:
- FaceDatabase 开始工作
- descriptors.yml 继续作为缓存
- ZMQ 命令已经能够操作 face_db.json
- **唯一问题**: 两个文件可能不同步，但不影响基本功能

**后续完善**:
- 添加同步逻辑
- 添加缓存失效检测

---

## 📄 配置更新

在 `vision_service.yaml` 中确认配置：

```yaml
face_recognition:
  # 是否启用描述符缓存
  enable_descriptor_cache: true
  
  # 描述符缓存文件路径（性能优化）
  descriptor_cache_path: "/home/pi/dev/nora-xiaozhi-dev/libs/FaceReco/descriptors.yml"
  
  # 🆕 人脸数据库路径（主数据源）
  face_database_path: "/home/pi/dev/nora-xiaozhi-dev/libs/FaceReco/data/face_db.json"
```

---

## ✅ 验收标准

- [ ] face_db.json 被正常加载和保存
- [ ] ZMQ register 命令写入 face_db.json
- [ ] ZMQ update 命令更新 face_db.json
- [ ] ZMQ delete 命令删除 face_db.json 记录
- [ ] descriptors.yml 与 face_db.json 保持同步
- [ ] 识别性能无明显下降（缓存生效）
- [ ] 人脸元数据（姓名、关系）正确显示

---

## 🎯 当前建议

基于你的需求和时间考虑，建议：

1. **立即执行**: 最小改动方案（30分钟）
2. **验证**: 测试 ZMQ 命令是否能操作 face_db.json
3. **后续优化**: Phase 2 缓存同步（可以明天做）

这样既解决了 "face_database.cpp 未使用" 的问题，又不会引入太大风险。

**需要我开始实施吗？**
