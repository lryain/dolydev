#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace doly::eye_engine {

struct TransitionSpec {
    std::string type{"param"};
    int duration_ms{0};
    std::string ease{"linear"};
    bool loop{false};
};

struct PupilParams {
    double min{0.0};
    double max{1.0};
    int response_ms{0};
};

struct DecorOffset {
    double x{0.0};
    double y{0.0};
};

struct DecorBitmapSpec {
    int width{0};
    int height{0};
    std::string format{"rgba8888"};
    std::string data_base64;
};

struct DecorLayerSpec {
    std::string id;
    std::string texture;
    std::string anchor{"eye"};
    std::string blend{"alpha"};
    double opacity{1.0};
    double scale{1.0};
    DecorOffset offset{};
    int z_index{0};
    bool enabled_by_default{false};
    std::vector<std::string> tags;
};

struct StylePackManifest {
    std::string name;
    std::string version;
    std::string engine;
    std::map<std::string, std::string> textures;
    std::map<std::string, std::string> frames;
    std::map<std::string, std::string> ui;
    std::vector<int> background_rgb;
    double iris_highlight{1.0};
    std::string pupil_shape{"round"};
    PupilParams pupil;
    std::string eyelid_params_path;
    TransitionSpec default_transition;
    std::map<std::string, TransitionSpec> transitions;
    std::string description;
    std::string author;
    std::vector<DecorLayerSpec> decor_layers;
    std::map<std::string, std::vector<std::string>> decor_groups;
};

struct StylePackLoadResult {
    StylePackManifest manifest;
    std::vector<std::string> warnings;
};

class StylePackLoader {
public:
    static std::optional<StylePackLoadResult> loadFromDirectory(const std::filesystem::path& root_dir);
};

}  // namespace doly::eye_engine
