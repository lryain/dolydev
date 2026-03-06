#pragma once
#include <vector>
#include <cstdint>
#include <thread>

// Keep the frame layout compatible with existing pylcd code
struct AnimationFrame {
    uint8_t* buffer; // ownership transferred to player
    int delay_ms;
};

class AnimationPlayer {
public:
    // Start playing; takes ownership of frames (buffers)
    static AnimationPlayer* start(std::vector<AnimationFrame>&& frames);
    // Stop and join thread; deletes the player
    void stop();
    ~AnimationPlayer();

private:
    AnimationPlayer(std::vector<AnimationFrame>&& frames);
    void run_loop();

    std::vector<AnimationFrame> frames_;
    bool running_;
    std::thread thread_;
};
