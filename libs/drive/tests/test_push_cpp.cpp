/**
 * @brief 最简 C++ PUSH 测试，排除 Python 因素
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/
#include <zmq.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

int main() {
    // 创建 context
    void* context = zmq_ctx_new();
    
    // 创建 PUSH socket
    void* pusher = zmq_socket(context, ZMQ_PUSH);
    
    // 连接到 drive_service PULL socket
    const char* endpoint = "ipc:///tmp/doly_control.sock";
    int rc = zmq_connect(pusher, endpoint);
    if (rc != 0) {
        fprintf(stderr, "Failed to connect: %s\n", zmq_strerror(errno));
        return 1;
    }
    printf("✓ Connected to %s\n", endpoint);
    
    // 等待连接建立
    sleep(1);
    
    // 发送 topic
    const char* topic = "io.pca9535.control.enable_servo_left";
    rc = zmq_send(pusher, topic, strlen(topic), ZMQ_SNDMORE);
    if (rc < 0) {
        fprintf(stderr, "Failed to send topic: %s\n", zmq_strerror(errno));
        return 1;
    }
    printf("✓ Sent topic: %s (%d bytes)\n", topic, rc);
    
    // 发送 JSON payload
    const char* payload = "{\"action\":\"enable_servo_left\",\"value\":true,\"timestamp\":1234567890,\"source\":\"cpp_test\"}";
    rc = zmq_send(pusher, payload, strlen(payload), 0);
    if (rc < 0) {
        fprintf(stderr, "Failed to send payload: %s\n", zmq_strerror(errno));
        return 1;
    }
    printf("✓ Sent payload: %s (%d bytes)\n", payload, rc);
    
    // 等待消息被消费
    printf("Waiting 5 seconds for message to be consumed...\n");
    sleep(5);
    
    // 清理
    zmq_close(pusher);
    zmq_ctx_term(context);
    printf("✓ Test completed\n");
    
    return 0;
}
