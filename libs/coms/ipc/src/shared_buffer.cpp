/**
 * @file shared_buffer.cpp
 * @brief 共享内存缓冲区实现
 */

#include "nora/coms/ipc/shared_buffer.h"

#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>
#include <stdexcept>
#include <system_error>

namespace nora::coms::ipc {

// ============================================================================
// SharedMemoryBuffer Implementation
// ============================================================================

SharedMemoryBuffer::SharedMemoryBuffer(key_t key, size_t size,
                                       bool create_if_not_exists)
    : data_(nullptr), size_(size), shmid_(-1) {
  int flags = SHM_R | SHM_W;

  if (create_if_not_exists) {
    flags |= IPC_CREAT;
  }

  shmid_ = shmget(key, size, flags);

  if (shmid_ == -1) {
    int err = errno;
    throw std::system_error(err, std::system_category(),
                            "shmget failed for key " + std::to_string(key));
  }

  // 附加到共享内存
  data_ = shmat(shmid_, nullptr, 0);

  if (data_ == reinterpret_cast<void*>(-1)) {
    int err = errno;
    data_ = nullptr;
    throw std::system_error(err, std::system_category(),
                            "shmat failed for shmid " + std::to_string(shmid_));
  }
}

SharedMemoryBuffer::~SharedMemoryBuffer() noexcept {
  if (data_ != nullptr) {
    shmdt(data_);
    data_ = nullptr;
  }
}

SharedMemoryBuffer::SharedMemoryBuffer(SharedMemoryBuffer&& other) noexcept
    : data_(other.data_), size_(other.size_), shmid_(other.shmid_) {
  other.data_ = nullptr;
  other.size_ = 0;
  other.shmid_ = -1;
}

SharedMemoryBuffer& SharedMemoryBuffer::operator=(
    SharedMemoryBuffer&& other) noexcept {
  if (this != &other) {
    if (data_ != nullptr) {
      shmdt(data_);
    }
    data_ = other.data_;
    size_ = other.size_;
    shmid_ = other.shmid_;

    other.data_ = nullptr;
    other.size_ = 0;
    other.shmid_ = -1;
  }
  return *this;
}

bool SharedMemoryBuffer::Remove(key_t key) {
  int shmid = shmget(key, 0, 0);
  if (shmid == -1) {
    return false;  // 不存在
  }

  if (shmctl(shmid, IPC_RMID, nullptr) == -1) {
    return false;
  }

  return true;
}

key_t SharedMemoryBuffer::MakeKey(const std::string& path, int project_id) {
  if (access(path.c_str(), F_OK) != 0) {
    throw std::system_error(errno, std::system_category(),
                            "Path does not exist: " + path);
  }

  key_t key = ftok(path.c_str(), project_id);
  if (key == -1) {
    throw std::system_error(errno, std::system_category(),
                            "ftok failed for path: " + path);
  }

  return key;
}

// ============================================================================
// CacheAlignedLayout Implementation
// ============================================================================

// CacheAlignedLayout 的所有方法都是 constexpr，无需实现

}  // namespace nora::coms::ipc
