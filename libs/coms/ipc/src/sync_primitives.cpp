/**
 * @file sync_primitives.cpp
 * @brief POSIX 同步原语实现
 */

#include "nora/coms/ipc/sync_primitives.h"

#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <iostream>
#include <sstream>

namespace nora::coms::ipc {

// ============================================================================
// Semaphore Implementation
// ============================================================================

Semaphore::Semaphore(const std::string& name, unsigned int initial_value,
                     bool create_if_not_exists)
    : name_(name), sem_(SEM_FAILED) {
  if (name_.empty() || name_[0] != '/') {
    throw std::system_error(EINVAL, std::system_category(),
                            "Semaphore name must start with /");
  }

  // 先尝试打开已存在的信号量
  sem_ = sem_open(name_.c_str(), 0);

  if (sem_ == SEM_FAILED) {
    if (!create_if_not_exists) {
      throw std::system_error(errno, std::system_category(),
                              "Failed to open semaphore: " + name_);
    }

    // 创建新信号量
    sem_ = sem_open(name_.c_str(), O_CREAT | O_EXCL, 0666, initial_value);

    if (sem_ == SEM_FAILED) {
      int err = errno;
      // 如果已存在，再次尝试打开
      if (err == EEXIST) {
        sem_ = sem_open(name_.c_str(), 0);
        if (sem_ == SEM_FAILED) {
          throw std::system_error(errno, std::system_category(),
                                  "Failed to open existing semaphore: " + name_);
        }
      } else {
        throw std::system_error(err, std::system_category(),
                                "Failed to create semaphore: " + name_);
      }
    }
  }
}

Semaphore::~Semaphore() noexcept {
  if (sem_ != SEM_FAILED) {
    sem_close(sem_);
  }
}

Semaphore::Semaphore(Semaphore&& other) noexcept
    : name_(std::move(other.name_)), sem_(other.sem_) {
  other.sem_ = SEM_FAILED;
}

Semaphore& Semaphore::operator=(Semaphore&& other) noexcept {
  if (this != &other) {
    if (sem_ != SEM_FAILED) {
      sem_close(sem_);
    }
    name_ = std::move(other.name_);
    sem_ = other.sem_;
    other.sem_ = SEM_FAILED;
  }
  return *this;
}

ErrorCode Semaphore::Wait() {
  if (sem_wait(sem_) == -1) {
    return ErrorCode::kSemaphoreWaitError;
  }
  return ErrorCode::kSuccess;
}

ErrorCode Semaphore::WaitTimeout(unsigned int timeout_ms) {
  if (timeout_ms == 0) {
    // 非阻塞模式
    return TryWait() ? ErrorCode::kSuccess : ErrorCode::kSemaphoreWaitTimeout;
  }

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);

  // 添加超时
  time_t sec = timeout_ms / 1000;
  long nsec = (timeout_ms % 1000) * 1000000;

  ts.tv_sec += sec;
  ts.tv_nsec += nsec;
  if (ts.tv_nsec >= 1000000000) {
    ts.tv_sec += 1;
    ts.tv_nsec -= 1000000000;
  }

  int result = sem_timedwait(sem_, &ts);
  if (result == -1) {
    if (errno == ETIMEDOUT) {
      return ErrorCode::kSemaphoreWaitTimeout;
    }
    return ErrorCode::kSemaphoreWaitError;
  }

  return ErrorCode::kSuccess;
}

bool Semaphore::TryWait() {
  return sem_trywait(sem_) == 0;
}

ErrorCode Semaphore::Post() {
  if (sem_post(sem_) == -1) {
    return ErrorCode::kSemaphorePostError;
  }
  return ErrorCode::kSuccess;
}

int Semaphore::GetValue() const {
  int value = 0;
  if (sem_getvalue(sem_, &value) == -1) {
    return -1;
  }
  return value;
}

ErrorCode Semaphore::Unlink(const std::string& name) {
  if (sem_unlink(name.c_str()) == -1) {
    if (errno == ENOENT) {
      return ErrorCode::kSuccess;  // 不存在也视为成功
    }
    return ErrorCode::kSemaphoreUnlinkError;
  }
  return ErrorCode::kSuccess;
}

// ============================================================================
// Mutex Implementation
// ============================================================================

Mutex::Mutex(bool is_recursive) {
  pthread_mutexattr_t attr;
  if (pthread_mutexattr_init(&attr) != 0) {
    throw std::system_error(errno, std::system_category(),
                            "Failed to init mutex attributes");
  }

  if (is_recursive) {
    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0) {
      pthread_mutexattr_destroy(&attr);
      throw std::system_error(errno, std::system_category(),
                              "Failed to set recursive mutex type");
    }
  }

  if (pthread_mutex_init(&mutex_, &attr) != 0) {
    pthread_mutexattr_destroy(&attr);
    throw std::system_error(errno, std::system_category(),
                            "Failed to init mutex");
  }

  pthread_mutexattr_destroy(&attr);
}

Mutex::~Mutex() noexcept {
  pthread_mutex_destroy(&mutex_);
}

ErrorCode Mutex::Lock() {
  if (pthread_mutex_lock(&mutex_) != 0) {
    return ErrorCode::kMutexLockError;
  }
  return ErrorCode::kSuccess;
}

bool Mutex::TryLock() {
  return pthread_mutex_trylock(&mutex_) == 0;
}

ErrorCode Mutex::LockTimeout(unsigned int timeout_ms) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);

  time_t sec = timeout_ms / 1000;
  long nsec = (timeout_ms % 1000) * 1000000;

  ts.tv_sec += sec;
  ts.tv_nsec += nsec;
  if (ts.tv_nsec >= 1000000000) {
    ts.tv_sec += 1;
    ts.tv_nsec -= 1000000000;
  }

  int result = pthread_mutex_timedlock(&mutex_, &ts);
  if (result != 0) {
    if (result == ETIMEDOUT) {
      return ErrorCode::kMutexTimedlockTimeout;
    }
    return ErrorCode::kMutexTimedlockError;
  }

  return ErrorCode::kSuccess;
}

ErrorCode Mutex::Unlock() {
  if (pthread_mutex_unlock(&mutex_) != 0) {
    return ErrorCode::kMutexUnlockError;
  }
  return ErrorCode::kSuccess;
}

// ============================================================================
// ScopedLock Implementation
// ============================================================================

ScopedLock::ScopedLock(Mutex& mutex, unsigned int timeout_ms)
    : mutex_(&mutex), is_locked_(false) {
  ErrorCode result;
  if (timeout_ms == 0) {
    result = mutex_->Lock();
  } else {
    result = mutex_->LockTimeout(timeout_ms);
  }

  if (result != ErrorCode::kSuccess) {
    throw std::system_error(static_cast<int>(result), std::system_category(),
                            "Failed to acquire lock");
  }

  is_locked_ = true;
}

ScopedLock::~ScopedLock() noexcept {
  if (is_locked_ && mutex_) {
    mutex_->Unlock();
  }
}

ScopedLock::ScopedLock(ScopedLock&& other) noexcept
    : mutex_(other.mutex_), is_locked_(other.is_locked_) {
  other.is_locked_ = false;
}

ScopedLock& ScopedLock::operator=(ScopedLock&& other) noexcept {
  if (this != &other) {
    if (is_locked_ && mutex_) {
      mutex_->Unlock();
    }
    mutex_ = other.mutex_;
    is_locked_ = other.is_locked_;
    other.is_locked_ = false;
  }
  return *this;
}

ErrorCode ScopedLock::Unlock() {
  if (!is_locked_) {
    return ErrorCode::kSuccess;
  }
  ErrorCode result = mutex_->Unlock();
  if (result == ErrorCode::kSuccess) {
    is_locked_ = false;
  }
  return result;
}

// ============================================================================
// Utility
// ============================================================================

std::string ErrorCodeToString(ErrorCode code) {
  switch (code) {
    case ErrorCode::kSuccess:
      return "Success";
    case ErrorCode::kSemaphoreCreateError:
      return "Semaphore creation failed";
    case ErrorCode::kSemaphoreWaitTimeout:
      return "Semaphore wait timeout";
    case ErrorCode::kSemaphoreWaitError:
      return "Semaphore wait error";
    case ErrorCode::kSemaphorePostError:
      return "Semaphore post error";
    case ErrorCode::kSemaphoreUnlinkError:
      return "Semaphore unlink error";
    case ErrorCode::kMutexInitError:
      return "Mutex initialization failed";
    case ErrorCode::kMutexLockError:
      return "Mutex lock error";
    case ErrorCode::kMutexUnlockError:
      return "Mutex unlock error";
    case ErrorCode::kMutexTimedlockTimeout:
      return "Mutex timed lock timeout";
    case ErrorCode::kMutexTimedlockError:
      return "Mutex timed lock error";
    default:
      return "Unknown error";
  }
}

}  // namespace nora::coms::ipc
