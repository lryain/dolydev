#include "msgbus_cpp/publisher.h"
#include "msgbus_cpp/message.h"
#include <iostream>
#include <chrono>
#include <thread>

int main(int argc, char **argv) {
    msgbus::Publisher pub("127.0.0.1", 5555);
    msgbus::Message m;
    m.type = msgbus::MessageType::VAD_SPEECH_SEGMENT;
    m.source = "example-publisher";
    std::string text = "hello from c++ publisher";
    m.data.assign(text.begin(), text.end());
    for (int i=0;i<50;i++){
        bool ok = pub.publish(m);
        std::cout << "published: " << ok << std::endl;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    return 0;
}
