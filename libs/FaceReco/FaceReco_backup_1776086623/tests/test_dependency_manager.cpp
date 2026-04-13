#include <gtest/gtest.h>
#include "vision/DependencyManager.h"
#include <memory>

using DependencyManagerPtr = std::shared_ptr<DependencyManager>;

class DependencyManagerTest : public ::testing::Test {
protected:
    DependencyManagerPtr dep_mgr;
    void SetUp() override { dep_mgr = CreateDependencyManager(); }
};

TEST_F(DependencyManagerTest, RegisterAndGetDependencies) {
    EXPECT_TRUE(dep_mgr->RegisterModule("A", {"B", "C"}));
    auto deps = dep_mgr->GetDependencies("A");
    EXPECT_EQ(deps.size(), 2);
    EXPECT_TRUE(dep_mgr->HasModule("A"));
}

TEST_F(DependencyManagerTest, GetDependents) {
    dep_mgr->RegisterModule("A", {});
    dep_mgr->RegisterModule("B", {"A"});
    dep_mgr->RegisterModule("C", {"A"});
    auto dependents = dep_mgr->GetDependents("A");
    EXPECT_EQ(dependents.size(), 2);
}

TEST_F(DependencyManagerTest, NoCyclicDependency) {
    dep_mgr->RegisterModule("A", {});
    dep_mgr->RegisterModule("B", {"A"});
    dep_mgr->RegisterModule("C", {"B"});
    EXPECT_FALSE(dep_mgr->HasCyclicDependency());
}

TEST_F(DependencyManagerTest, HasCyclicDependency) {
    dep_mgr->RegisterModule("A", {"B"});
    dep_mgr->RegisterModule("B", {"A"});
    EXPECT_TRUE(dep_mgr->HasCyclicDependency());
}

TEST_F(DependencyManagerTest, InitializationOrder) {
    dep_mgr->RegisterModule("A", {});
    dep_mgr->RegisterModule("B", {"A"});
    dep_mgr->RegisterModule("C", {"A", "B"});
    auto order = dep_mgr->GetInitializationOrder();
    EXPECT_EQ(order.size(), 3);
    // A 应该在 B 之前
    auto pos_a = std::find(order.begin(), order.end(), "A");
    auto pos_b = std::find(order.begin(), order.end(), "B");
    EXPECT_TRUE(pos_a < pos_b);
}

TEST_F(DependencyManagerTest, UnloadOrder) {
    dep_mgr->RegisterModule("A", {});
    dep_mgr->RegisterModule("B", {"A"});
    dep_mgr->RegisterModule("C", {"B"});
    auto unload = dep_mgr->GetUnloadOrder();
    EXPECT_EQ(unload.size(), 3);
    // C 应该在 B 之前卸载
    auto pos_c = std::find(unload.begin(), unload.end(), "C");
    auto pos_b = std::find(unload.begin(), unload.end(), "B");
    EXPECT_TRUE(pos_c < pos_b);
}

TEST_F(DependencyManagerTest, IsDependencySatisfied) {
    dep_mgr->RegisterModule("A", {"B", "C"});
    EXPECT_TRUE(dep_mgr->IsDependencySatisfied("A", {"B", "C", "D"}));
    EXPECT_FALSE(dep_mgr->IsDependencySatisfied("A", {"B"}));
}

TEST_F(DependencyManagerTest, AreAllDependenciesSatisfied) {
    dep_mgr->RegisterModule("A", {});
    dep_mgr->RegisterModule("B", {"A"});
    EXPECT_TRUE(dep_mgr->AreAllDependenciesSatisfied({"A", "B"}));
    EXPECT_FALSE(dep_mgr->AreAllDependenciesSatisfied({"B"}));
}

TEST_F(DependencyManagerTest, GetAllModules) {
    dep_mgr->RegisterModule("A", {});
    dep_mgr->RegisterModule("B", {});
    auto modules = dep_mgr->GetAllModules();
    EXPECT_EQ(modules.size(), 2);
}

TEST_F(DependencyManagerTest, ComplexDependency) {
    dep_mgr->RegisterModule("LightDetection", {});
    dep_mgr->RegisterModule("MotionDetection", {});
    dep_mgr->RegisterModule("FaceDetection", {"LightDetection"});
    dep_mgr->RegisterModule("FaceRecognition", {"FaceDetection"});
    dep_mgr->RegisterModule("Visualizer", {"FaceDetection", "MotionDetection"});
    
    EXPECT_FALSE(dep_mgr->HasCyclicDependency());
    auto order = dep_mgr->GetInitializationOrder();
    EXPECT_EQ(order.size(), 5);
}

TEST_F(DependencyManagerTest, GetSummary) {
    dep_mgr->RegisterModule("A", {"B"});
    std::string summary = dep_mgr->GetSummary();
    EXPECT_TRUE(summary.find("A") != std::string::npos);
    EXPECT_TRUE(summary.find("B") != std::string::npos);
}

TEST_F(DependencyManagerTest, Clear) {
    dep_mgr->RegisterModule("A", {});
    EXPECT_EQ(dep_mgr->GetAllModules().size(), 1);
    dep_mgr->Clear();
    EXPECT_EQ(dep_mgr->GetAllModules().size(), 0);
}
