#include "msgbus_cpp/subscriber.h"
#include <iostream>

int main(int argc, char **argv) {
    msgbus::Subscriber sub("0.0.0.0", 5555);
    bool ok = sub.start([](const msgbus::Message &m){
        std::string s(m.data.begin(), m.data.end());
        std::cout << "received type=" << static_cast<int>(m.type) << " source=" << m.source << " data=" << s << std::endl;
    });
    if (!ok) {
        std::cerr << "failed to start subscriber" << std::endl;
        return 1;
    }
    std::cout << "subscriber running... press Ctrl-C to quit" << std::endl;
    // sleep forever
    while (true) std::this_thread::sleep_for(std::chrono::seconds(10));
    return 0;
}
