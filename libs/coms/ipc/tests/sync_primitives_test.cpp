/**
 * @file sync_primitives_test.cpp
 * @brief 同步原语单元测试
 */

#include "nora/coms/ipc/sync_primitives.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

using namespace nora::coms::ipc;

bool test_semaphore_basic() {
  std::cout << "[Test] Basic Semaphore Operations\n";

  try {
    Semaphore sem("/test_sem_basic", 1);

    // 测试 TryWait
    if (!sem.TryWait()) {
      std::cout << "  ✗ TryWait failed\n";
      return false;
    }

    // 再次 TryWait 应该失败
    if (sem.TryWait()) {
      std::cout << "  ✗ Second TryWait should fail\n";
      return false;
    }

    // 测试 Post
    if (sem.Post() != ErrorCode::kSuccess) {
      std::cout << "  ✗ Post failed\n";
      return false;
    }

    // 现在 TryWait 应该成功
    if (!sem.TryWait()) {
      std::cout << "  ✗ TryWait after Post failed\n";
      return false;
    }

    Semaphore::Unlink("/test_sem_basic");
    std::cout << "  ✓ Basic Semaphore Operations passed\n";
    return true;

  } catch (const std::exception& e) {
    std::cout << "  ✗ Exception: " << e.what() << "\n";
    return false;
  }
}

bool test_semaphore_timeout() {
  std::cout << "[Test] Semaphore Wait Timeout\n";

  try {
    Semaphore sem("/test_sem_timeout", 0);  // 初始为 0

    // 等待应该超时
    auto result = sem.WaitTimeout(100);
    if (result != ErrorCode::kSemaphoreWaitTimeout) {
      std::cout << "  ✗ Expected timeout, got: " << static_cast<int>(result)
                << "\n";
      return false;
    }

    Semaphore::Unlink("/test_sem_timeout");
    std::cout << "  ✓ Semaphore Timeout passed\n";
    return true;

  } catch (const std::exception& e) {
    std::cout << "  ✗ Exception: " << e.what() << "\n";
    return false;
  }
}

bool test_mutex_basic() {
  std::cout << "[Test] Basic Mutex Operations\n";

  try {
    Mutex mutex;

    // 测试 TryLock
    if (!mutex.TryLock()) {
      std::cout << "  ✗ TryLock failed\n";
      return false;
    }

    // 再次 TryLock 应该失败（非递归）
    if (mutex.TryLock()) {
      std::cout << "  ✗ Second TryLock should fail (non-recursive)\n";
      mutex.Unlock();
      return false;
    }

    // 解锁
    if (mutex.Unlock() != ErrorCode::kSuccess) {
      std::cout << "  ✗ Unlock failed\n";
      return false;
    }

    // 现在应该可以再次锁定
    if (!mutex.TryLock()) {
      std::cout << "  ✗ TryLock after Unlock failed\n";
      return false;
    }

    mutex.Unlock();

    std::cout << "  ✓ Basic Mutex Operations passed\n";
    return true;

  } catch (const std::exception& e) {
    std::cout << "  ✗ Exception: " << e.what() << "\n";
    return false;
  }
}

bool test_recursive_mutex() {
  std::cout << "[Test] Recursive Mutex\n";

  try {
    Mutex mutex(true);  // 递归锁

    // 多次锁定应该成功
    if (mutex.Lock() != ErrorCode::kSuccess) {
      std::cout << "  ✗ First Lock failed\n";
      return false;
    }

    if (mutex.Lock() != ErrorCode::kSuccess) {
      std::cout << "  ✗ Second Lock failed (should succeed for recursive)\n";
      return false;
    }

    // 多次解锁
    if (mutex.Unlock() != ErrorCode::kSuccess) {
      std::cout << "  ✗ First Unlock failed\n";
      return false;
    }

    if (mutex.Unlock() != ErrorCode::kSuccess) {
      std::cout << "  ✗ Second Unlock failed\n";
      return false;
    }

    std::cout << "  ✓ Recursive Mutex passed\n";
    return true;

  } catch (const std::exception& e) {
    std::cout << "  ✗ Exception: " << e.what() << "\n";
    return false;
  }
}

bool test_scoped_lock() {
  std::cout << "[Test] ScopedLock RAII\n";

  try {
    Mutex mutex;

    {
      ScopedLock lock(mutex);

      // 在作用域内持有锁
      if (!mutex.TryLock()) {
        // 期望失败，因为已被锁定
        std::cout << "  ✓ Lock is held in scope\n";
      } else {
        std::cout << "  ✗ Lock should be held\n";
        mutex.Unlock();
        return false;
      }
    }

    // 离开作用域后应该自动解锁
    if (!mutex.TryLock()) {
      std::cout << "  ✗ Lock should be released after scope\n";
      return false;
    }

    mutex.Unlock();

    std::cout << "  ✓ ScopedLock RAII passed\n";
    return true;

  } catch (const std::exception& e) {
    std::cout << "  ✗ Exception: " << e.what() << "\n";
    return false;
  }
}

int main() {
  std::cout << "\n=== Sync Primitives Tests ===\n\n";

  std::vector<bool> results;

  results.push_back(test_semaphore_basic());
  results.push_back(test_semaphore_timeout());
  results.push_back(test_mutex_basic());
  results.push_back(test_recursive_mutex());
  results.push_back(test_scoped_lock());

  std::cout << "\n=== Test Summary ===\n";
  int passed = 0;
  for (bool result : results) {
    if (result) passed++;
  }

  std::cout << "Passed: " << passed << "/" << results.size() << "\n\n";

  return passed == results.size() ? 0 : 1;
}
