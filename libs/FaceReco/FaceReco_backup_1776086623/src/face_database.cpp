#include "doly/vision/face_database.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

namespace doly::vision {

FaceDatabase::FaceDatabase(std::string storage_path)
    : storage_path_(std::move(storage_path)) {}

void FaceDatabase::setStoragePath(std::string storage_path) {
    storage_path_ = std::move(storage_path);
}

bool FaceDatabase::load() {
    records_.clear();
    std::ifstream in(storage_path_);
    if (!in.good()) {
        return true;
    }

    nlohmann::json data;
    in >> data;
    if (!data.is_array()) {
        return false;
    }

    for (const auto& item : data) {
        FaceRecord record;
        record.face_id = item.value("face_id", "");
        record.name = item.value("name", "");
        record.image_path = item.value("image_path", "");
        record.metadata = item.value("metadata", nlohmann::json::object());
        record.created_at = item.value("created_at", "");
        record.last_seen = item.value("last_seen", "");
        record.sample_count = item.value("sample_count", 0);
        if (record.face_id.empty()) {
            continue;
        }
        records_[record.face_id] = record;
    }

    return true;
}

bool FaceDatabase::save() const {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(fs::path(storage_path_).parent_path(), ec);

    nlohmann::json data = nlohmann::json::array();
    for (const auto& [_, record] : records_) {
        data.push_back({
            {"face_id", record.face_id},
            {"name", record.name},
            {"image_path", record.image_path},
            {"metadata", record.metadata},
            {"created_at", record.created_at},
            {"last_seen", record.last_seen},
            {"sample_count", record.sample_count}
        });
    }

    std::ofstream out(storage_path_);
    if (!out.good()) {
        return false;
    }

    out << data.dump(2);
    return true;
}

std::vector<FaceRecord> FaceDatabase::list() const {
    std::vector<FaceRecord> result;
    result.reserve(records_.size());
    for (const auto& [_, record] : records_) {
        result.push_back(record);
    }
    return result;
}

std::optional<FaceRecord> FaceDatabase::get(const std::string& face_id) const {
    auto it = records_.find(face_id);
    if (it == records_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<FaceRecord> FaceDatabase::getByName(const std::string& name) const {
    if (name.empty()) {
        return std::nullopt;
    }
    for (const auto& [_, record] : records_) {
        if (record.name == name) {
            return record;
        }
    }
    return std::nullopt;
}

bool FaceDatabase::addOrUpdate(const FaceRecord& record) {
    if (record.face_id.empty()) {
        return false;
    }
    records_[record.face_id] = record;
    return true;
}

bool FaceDatabase::remove(const std::string& face_id) {
    return records_.erase(face_id) > 0;
}

}  // namespace doly::vision
