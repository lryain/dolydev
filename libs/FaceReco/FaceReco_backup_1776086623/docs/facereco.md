- FaceReco 功能测试要先清理 libs/FaceReco/tmp/functional_zmq_test，避免旧的 face_db.json、descriptor cache 和样本图污染结果。
- FaceReco 的抓拍/录像队列不能只依赖 enabled 唤醒；IDLE 期间必须让 wait 条件包含 pending capture，否则会出现“命令已入队但永不消费”，尤其在拍照后立刻切回 IDLE 时最明显。

- 文件视频源到 EOF 时，ParallelVideoCapture 必须把 running_ 清零；否则主循环不会重开 source，也会卡住 video_stop 处理。
- math.hpp 的 zScore 需要防 0 方差；NaN 描述子会让 getClosestFaceDescriptorPersonName 的相似度选择失效，表现为注册成功但始终识别不到。 
- ZMQ 的 status.vision.ready 是单次广播，测试侧最好接受 status.vision.state 作为 readiness fallback；服务侧重复广播几次 ready 更稳。