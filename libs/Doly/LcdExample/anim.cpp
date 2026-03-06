// Thin wrapper that forwards to pylcd exported C API.
#include <iostream>
#include <cstring>

extern "C" {
    // exported from pylcd.cpp
    int lcd_main();
    int show_eye_test(int type, const char* input);
}

int main(int argc, char** argv) {
    if (argc > 1) {
        const char* input = argv[1];
        std::cout << "anim: calling show_eye_test for folder: " << input << std::endl;
        return show_eye_test(3, input);
    } else {
        std::cout << "anim: calling lcd_main demo" << std::endl;
        return lcd_main();
    }
}