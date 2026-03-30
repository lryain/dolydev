#include "autocharge/autocharge_service.hpp"
#include "autocharge/config_loader.hpp"

#include <getopt.h>

#include <cstdlib>
#include <iostream>

using doly::autocharge::AutoChargeService;
using doly::autocharge::ServiceConfig;

namespace {

void printUsage(const char* program) {
    std::cout
        << "Usage: " << program << " [options]\n\n"
        << "Options:\n"
        << "  -c, --config PATH      YAML config file (default /home/pi/dolydev/config/autocharge.yaml)\n"
        << "  --marker-id N          Target marker id (default 23)\n"
        << "  --marker-size-m M      Marker size in meters (default 0.12)\n"
        << "  --video-width N        Camera width (default 1280)\n"
        << "  --video-height N       Camera height (default 960)\n"
        << "  --lcd-side left|right  LCD side for debug view (default right)\n"
        << "  --no-drive             Disable motion execution\n"
        << "  --no-lcd               Disable LCD output\n"
        << "  --dump-dir PATH        Save debug camera frames into PATH\n"
        << "  --max-frames N         Max frames before timeout\n"
        << "  -h, --help             Show this help\n";
}

}  // namespace

int main(int argc, char* argv[]) {
    ServiceConfig config;
    std::string config_path = "/home/pi/dolydev/config/autocharge.yaml";

    static option options[] = {
        {"config", required_argument, nullptr, 'c'},
        {"marker-id", required_argument, nullptr, 1},
        {"marker-size-m", required_argument, nullptr, 2},
        {"video-width", required_argument, nullptr, 3},
        {"video-height", required_argument, nullptr, 4},
        {"lcd-side", required_argument, nullptr, 5},
        {"no-drive", no_argument, nullptr, 6},
        {"no-lcd", no_argument, nullptr, 7},
        {"max-frames", required_argument, nullptr, 8},
        {"dump-dir", required_argument, nullptr, 9},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };

    for (int index = 1; index < argc; ++index) {
        if ((std::string(argv[index]) == "--config" || std::string(argv[index]) == "-c") && index + 1 < argc) {
            config_path = argv[index + 1];
            ++index;
        }
    }

    std::string load_error;
    if (!loadServiceConfig(config_path, config, &load_error)) {
        std::cerr << "[AutoCharge] Failed to load config " << config_path << ": " << load_error << std::endl;
        return 1;
    }

    optind = 1;

    while (true) {
        const int opt = getopt_long(argc, argv, "c:h", options, nullptr);
        if (opt == -1) {
            break;
        }
        switch (opt) {
            case 'c':
                break;
            case 1:
                config.marker_id = std::atoi(optarg);
                break;
            case 2:
                config.marker_size_m = std::strtof(optarg, nullptr);
                break;
            case 3:
                config.video_width = std::atoi(optarg);
                break;
            case 4:
                config.video_height = std::atoi(optarg);
                break;
            case 5:
                config.lcd_side = (std::string(optarg) == "left") ? 0 : 1;
                break;
            case 6:
                config.enable_drive = false;
                break;
            case 7:
                config.enable_lcd = false;
                break;
            case 8:
                config.max_frames = std::atoi(optarg);
                break;
            case 9:
                config.dump_dir = optarg;
                break;
            case 'h':
            default:
                printUsage(argv[0]);
                return 0;
        }
    }

    AutoChargeService service(config);
    if (!service.initialize()) {
        return 1;
    }
    const int rc = service.run();
    service.shutdown();
    return rc;
}