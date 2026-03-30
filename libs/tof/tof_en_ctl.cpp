#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include "../../libs/Doly/include/Gpio.h"

// Small CLI to control TOF_ENL (TOF_ENL = 64) via Doly GPIO driver
// Usage: ./tof_en_ctl on|off|pulse [ms]
// Return codes: 0 ok, 2 bad args, 3 init fail, 4 write fail

static int usage(const char* prog){
    std::fprintf(stderr, "Usage: %s on|off|pulse [ms] [pin]\n", prog);
    std::fprintf(stderr, "       %s on|off --pin N\n", prog);
    return 2;
}

static int parse_pin_arg(int argc, char** argv, int start_index, int default_pin){
    // support: positional numeric pin at argv[start_index]
    // or --pin N or --pin=N
    if (start_index >= argc) return default_pin;
    const char* a = argv[start_index];
    if (!a) return default_pin;
    // --pin=N
    if (std::strncmp(a, "--pin=", 6) == 0){
        return std::atoi(a + 6);
    }
    // --pin N
    if (std::strcmp(a, "--pin") == 0){
        if (start_index+1 < argc) return std::atoi(argv[start_index+1]);
        return default_pin;
    }
    // -p N
    if (std::strcmp(a, "-p") == 0){
        if (start_index+1 < argc) return std::atoi(argv[start_index+1]);
        return default_pin;
    }
    // positional numeric
    bool all_digit = true;
    for (const char* s = a; *s; ++s){ if (!((*s >= '0' && *s <= '9') || (*s=='-'))) { all_digit = false; break; } }
    if (all_digit) return std::atoi(a);
    return default_pin;
}

int main(int argc, char** argv){
    if (argc < 2) return usage(argv[0]);
    std::string cmd = argv[1];

    // default pin is TOF_ENL
    int default_pin = static_cast<int>(TOF_ENL);
    int specified_pin = parse_pin_arg(argc, argv, 2, default_pin);
    // if --pin was used as argv[1] (edge case), also parse from argv[1]
    if (specified_pin == default_pin && std::strncmp(argv[1], "--pin=", 6) == 0){
        specified_pin = parse_pin_arg(argc, argv, 1, default_pin);
    }

    PinId target = static_cast<PinId>(specified_pin);

    if (GPIO::init(target, GpioType::GPIO_OUTPUT, GpioState::LOW) != 0){
        std::fprintf(stderr, "[ERR] init pin %d failed\n", specified_pin);
        return 3;
    }

    if (cmd == "on"){
    if (GPIO::writePin(target, GpioState::HIGH) != 0){
            std::fprintf(stderr, "[ERR] write HIGH failed (pin %d)\n", specified_pin);
            return 4;
        }
        return 0;
    }
    if (cmd == "off"){
    if (GPIO::writePin(target, GpioState::LOW) != 0){
            std::fprintf(stderr, "[ERR] write LOW failed (pin %d)\n", specified_pin);
            return 4;
        }
        return 0;
    }
    if (cmd == "pulse"){
        int ms = (argc >= 3) ? std::stoi(argv[2]) : 10;
    if (GPIO::writePin(target, GpioState::LOW) != 0){
            std::fprintf(stderr, "[ERR] set LOW failed (pin %d)\n", specified_pin);
            return 4;
        }
        usleep(1000*ms);
    if (GPIO::writePin(target, GpioState::HIGH) != 0){
            std::fprintf(stderr, "[ERR] set HIGH failed (pin %d)\n", specified_pin);
            return 4;
        }
        return 0;
    }
    return usage(argv[0]);
}
