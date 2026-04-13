
## Run

make sure you have changed  `project_path` to your own

```shell
mkdir build
cd build
cmake ..
make -j2
./LiveFaceReco
```

---

## Vision Service (Doly)

FaceReco 已集成为 Doly 视觉服务入口（`vision_service.cpp`），支持 ZMQ 命令与查询。

### 运行

```shell
cd /home/pi/dolydev/libslibs/FaceReco/build
./LiveFaceReco
```

### 配置

配置文件：`/home/pi/dolydev/libslibs/FaceReco/config.ini`

关键字段：

- `vision_pub_endpoint`
- `vision_sub_endpoint`
- `vision_query_endpoint`
- `vision_initial_mode`
- `vision_enabled`
- `vision_streaming_enabled`
- `stream_only`
- `stream_publish_always`
- `vision_face_db_path`

---

## Tests

### E2E (dry-run 默认)

```shell
python3 /home/pi/dolydev/libsscripts/test_vision_e2e.py
```

### 性能 (dry-run 默认)

```shell
python3 /home/pi/dolydev/libsscripts/test_vision_performance.py
```

### 稳定性 (dry-run 默认)

```shell
python3 /home/pi/dolydev/libsscripts/test_vision_stability.py
```

---

## Deployment (systemd)

部署脚本位于：`/home/pi/dolydev/libslibs/FaceReco/scripts/manage_service.sh`

常用命令：

```shell
cd /home/pi/dolydev/libslibs/FaceReco/scripts
./manage_service.sh build
./manage_service.sh install
./manage_service.sh start
```

---

## Adjustable Parameters

1. **largest_face_only:** only detects the largest face
2. **record_face:** add face to database
3. **distance_threshold:** avoid recognize face which is far away (default 90)
4. **face_thre:** threshold for Recognition (default 0.40)
5. **true_thre:** threshold for Anti Spoofing (default 0.89)
6. **jump:** jump some frames to accelerate
7. **input_width:** set input width (recommend 320)
8. **input_height:** set input height (recommend 240)
9. **output_width:** set output width (recommend 320)
10. **output_height:** set input height (recommend 240)
11. **project_path:** set to your own path
12. **angle_threshold:** threshold for face angle (default 15 degrees) 超过这个角度就不进行识别

13. enable_recognition 改为 enable_continuous_recognition 更直观，现在有个bug是enable_continuous_recognition=false，程序压根就不进行识别了，只进行跟踪，正确的是按照recognition_max_attempts = 3，recognition_reset_frames = 10 指定的逻辑识别，然后才是只进行跟踪；enable_continuous_recognition=true，目前有问题，如果检测到人脸没有连续识别，正确的是进行连续识别也就是说跟踪框在人脸角度满足angle_threshold=45.0的情况一直进行识别并且是绿色的才对！而且enable_continuous_recognition不管true或者false都要在人脸（无论是否之前识别过）消失再次出现的情况下重新识别！

# bug

[TRACK 2] Fake face!!
copy_cut_border parameter error, top: 241, bottom: 1, left: 155, right: 108, src.w: 320, src.h: 240
Segmentation fault

统一下信息都打印，信息打印到到每个人脸框里:
左上加：
第一行：[tracking/reco]: name [fake/real] [置信度]
右上角：面部角度
然后跟踪框的颜色：跟踪时候是黄色，识别成功是绿色，识别失败/未知是红色

文字超出了识别框，字号能不能跟随识别框大小自适应，然后调整字体颜色默认为绿色：
然后把原来框内右下，调整到左下位置，fake字段是应该用红色显示

record_face=true 的情况需要询问用户是否注册此用户：可以直接输入人名注册，如果不输入直接回车则不进行注册！

