#include <gtest/gtest.h>
#include "doly/vision/face_database.hpp"

#include <cstdio>

using doly::vision::FaceDatabase;
using doly::vision::FaceRecord;

namespace {
std::string tempPath() {
    return "/tmp/face_db_test.json";
}
}

TEST(FaceDatabaseTest, AddListRemove) {
    auto path = tempPath();
    std::remove(path.c_str());

    FaceDatabase db(path);
    FaceRecord rec;
    rec.face_id = "id-1";
    rec.name = "alice";
    rec.image_path = "/tmp/a.jpg";
    rec.metadata = nlohmann::json{{"relation", "friend"}};
    rec.created_at = "2026-02-08";
    rec.last_seen = "2026-02-08";
    rec.sample_count = 1;

    EXPECT_TRUE(db.addOrUpdate(rec));
    EXPECT_EQ(db.list().size(), 1u);

    EXPECT_TRUE(db.save());

    FaceDatabase db2(path);
    EXPECT_TRUE(db2.load());
    EXPECT_EQ(db2.list().size(), 1u);
    EXPECT_TRUE(db2.getByName("alice").has_value());

    EXPECT_TRUE(db2.remove("id-1"));
    EXPECT_EQ(db2.list().size(), 0u);
}
