#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace doly::eye_engine {

/**
 * Simple RGBA texture loaded from PNG.
 */
struct Texture {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba_data;  // 4 bytes per pixel: R, G, B, A

    bool empty() const { return rgba_data.empty(); }
    std::size_t size_bytes() const { return rgba_data.size(); }
};

/**
 * Loads PNG textures for decoration rendering.
 */
class TextureLoader {
public:
    TextureLoader();
    ~TextureLoader();

    /**
     * Load a PNG file into a Texture.
     * Returns empty Texture on failure.
     */
    Texture load(const std::string& file_path) const;

private:
    // Disable copy/move
    TextureLoader(const TextureLoader&) = delete;
    TextureLoader& operator=(const TextureLoader&) = delete;
    TextureLoader(TextureLoader&&) = delete;
    TextureLoader& operator=(TextureLoader&&) = delete;
};

}  // namespace doly::eye_engine