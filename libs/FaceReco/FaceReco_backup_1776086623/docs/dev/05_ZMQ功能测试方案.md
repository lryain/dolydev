# FaceReco ZMQ 功能测试方案

> 更新日期: 2026-04-12
> 适用对象: 需要直接运行程序、观察日志、复现问题并做功能回归的开发者

---

## 1. 目标

本方案用于验证 FaceReco 通过 ZMQ 暴露的关键功能是否完整闭环，并保证测试结果满足以下要求:

- 不是 ctest，而是可直接执行的普通程序
- 输出明确的 PASS、FAIL、INFO、EVENT、SERVICE 日志
- 每个功能点都有可观察的成功判据
- 运行时不依赖真实人工终端交互
- 支持隔离环境下重复回归

---

## 2. 测试入口

### 2.1 编译

```bash
cd /home/pi/dolydev/libs/FaceReco/build
make -j2
```

### 2.2 执行测试

```bash
cd /home/pi/dolydev/libs/FaceReco
scripts/run_vision_zmq_functional_test.sh
```

### 2.3 关键脚本

| 路径 | 说明 |
|------|------|
| scripts/run_vision_zmq_functional_test.sh | shell 入口 |
| scripts/vision_zmq_functional_test.py | 主测试程序 |

---

## 3. 测试策略

### 3.1 隔离运行环境

测试会自动创建并清理独立工作目录:

```text
libs/FaceReco/tmp/functional_zmq_test/
```

其中包含:

- media: 生成的合成视频
- captures: 拍照结果
- videos: 录像结果
- data/face_db.json: 隔离数据库
- data/descriptors.yml: 隔离描述子缓存
- functional_test.log: 主日志

### 3.2 隔离 ZMQ 端点

测试会为 command、event、query 分别生成临时 IPC socket，避免和系统其他进程冲突。

### 3.3 文件视频源替代真实摄像头

测试通过两段合成视频驱动:

- known_subject.avi: 已知身份视频
- unknown_subject.avi: 陌生身份视频

这样可以保证每次输入稳定、可重复。

### 3.4 日志分级

测试输出统一采用以下前缀:

- PASS: 当前功能点通过
- FAIL: 当前功能点失败
- INFO: 测试阶段说明
- EVENT: 收到的事件总线消息
- SERVICE: FaceReco 服务自身输出

---

## 4. 用例设计与验收标准

### 4.1 服务启动

目的:

- 确认服务能够正常启动
- 确认事件总线和查询总线可用

判定:

- 收到 status.vision.ready，或收到有效的 status.vision.state 作为 readiness fallback

### 4.2 DETECT_ONLY 模式

目的:

- 验证模式切换
- 验证检测与跟踪持续输出

判定:

- cmd.vision.mode 切换为 DETECT_ONLY 后收到状态确认
- 连续两次 event.vision.face 指向同一 tracker，说明跟踪稳定

### 4.3 FULL 模式下拍照

目的:

- 验证拍照命令和结果事件

判定:

- 收到 event.vision.capture.complete 且 type=photo
- 输出文件存在且文件大小大于 0

### 4.4 FULL 模式下录像

目的:

- 验证录像开始、停止和保存

判定:

- 收到 event.vision.capture.started 且 status=recording
- 发送 cmd.vision.capture.video.stop 后收到 event.vision.capture.complete
- 输出视频文件存在且文件大小大于 0

### 4.5 STREAM_ONLY 模式

目的:

- 验证视频推流模式下抑制检测事件

判定:

- 模式切换成功
- quiet window 内不出现 event.vision.face 和 event.vision.face.recognized

### 4.6 陌生人发现

目的:

- 验证新身份被标记并发出注册提示

判定:

- 收到 event.vision.face.new
- 事件中包含 tracker_id

### 4.7 当前跟踪人脸注册

目的:

- 验证 cmd.vision.face.register(method=current) 闭环

判定:

- 收到 event.vision.face.registered
- success=true
- 返回 face_id、name、image_path、sample_count

### 4.8 人脸列表查询

目的:

- 验证 query.vision.face.list 返回完整元数据

判定:

- 查询结果中包含 known_subject 和新注册身份

### 4.9 注册后识别

目的:

- 验证热更新描述子后，无需重启服务即可识别刚注册的人脸

判定:

- 收到 event.vision.face.recognized
- name 为新注册标签

### 4.10 更新后识别

目的:

- 验证 rename 会同步影响后续识别结果

判定:

- event.vision.face.updated success=true
- 后续 event.vision.face.recognized 命中新标签

### 4.11 删除与查询回归

目的:

- 验证删除后查询结果一致

判定:

- event.vision.face.deleted success=true
- query.vision.face.list 不再返回已删除身份

---

## 5. 当前实现说明

### 5.1 强校验项

当前脚本把以下项作为阻断式验收:

- service ready
- detect_only 跟踪稳定
- photo capture
- video recording
- stream_only 抑制检测事件
- unknown face new event
- register current
- query.vision.face.list
- post-registration recognition
- rename and recognition-after-rename
- delete and query-after-delete

### 5.2 观测项

当前仍保留一项观测而非阻断:

- cold-start known-face recognition on synthetic video

原因是它受合成视频插值、单样本预置和初始姿态影响较大，不适合作为这一轮的唯一阻断指标。

---

## 6. 最近一次实测结果

最近一次完整执行结果:

- all functional tests passed

已明确通过的关键日志包括:

- photo capture succeeded
- video recording succeeded
- stream_only suppressed face detection events as expected
- current face registration succeeded
- newly registered face recognized as new_subject
- face update renamed new_subject -> new_subject_renamed
- renamed face recognized as new_subject_renamed
- face delete removed new_subject_renamed
- query.vision.face.list no longer returns deleted face

---

## 7. 常见失败点与排查建议

### 7.1 启动卡在 ready

排查:

- 检查 IPC socket 是否被其他进程占用
- 检查 functional_test.log 中是否至少出现 status.vision.state
- 确认 LiveFaceReco 启动配置是否指向测试生成的 INI

### 7.2 注册成功但无法识别

排查:

- 检查 descriptors.yml 是否写入新标签
- 检查日志中是否存在 person_name=empty 持续出现
- 检查描述子是否存在 NaN 或非法值

### 7.3 录像无法结束

排查:

- 检查是否收到了 cmd.vision.capture.video.stop
- 检查文件视频源是否在 EOF 后正确重开
- 检查是否发布了 event.vision.capture.complete

---

## 8. 建议的后续扩展

建议后续将以下项纳入自动化功能测试:

1. recapture 功能
2. query.vision.face.position 功能
3. 真实摄像头输入下的冷启动已注册识别
4. 多人同时进入画面时的跟踪与识别稳定性
5. 长时间运行稳定性与 descriptor cache 一致性回归