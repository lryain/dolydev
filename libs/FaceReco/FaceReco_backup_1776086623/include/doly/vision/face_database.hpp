#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <nlohmann/json.hpp>

namespace doly::vision {

struct FaceRecord {
    std::string face_id;
    std::string name;
    std::string image_path;
    nlohmann::json metadata;
    std::string created_at;
    std::string last_seen;
    int sample_count{0};
};

class FaceDatabase {
public:
    explicit FaceDatabase(std::string storage_path);

    void setStoragePath(std::string storage_path);

    bool load();
    bool save() const;

    std::vector<FaceRecord> list() const;
    std::optional<FaceRecord> get(const std::string& face_id) const;
    std::optional<FaceRecord> getByName(const std::string& name) const;

    bool addOrUpdate(const FaceRecord& record);
    bool remove(const std::string& face_id);

private:
    std::string storage_path_;
    std::unordered_map<std::string, FaceRecord> records_;
};

}  // namespace doly::vision
