/**
 * @file sync_primitives.h
 * @brief POSIX 同步原语 RAII 包装器
 * 
 * 提供线程安全的同步机制：
 * - Semaphore: 命名信号量，支持超时
 * - Mutex: pthread_mutex_t，支持递归和超时
 * - ScopedLock: 自动资源管理的锁守卫
 * 
 * @author Nora Project
 * @version 1.0.0
 */

#pragma once

#include <semaphore.h>
#include <pthread.h>
#include <chrono>
#include <memory>
#include <string>
#include <system_error>

namespace nora::coms::ipc {

/**
 * @brief 错误码定义
 */
enum class ErrorCode {
  kSuccess = 0,
  kSemaphoreCreateError = 1001,
  kSemaphoreWaitTimeout = 1002,
  kSemaphoreWaitError = 1003,
  kSemaphorePostError = 1004,
  kSemaphoreUnlinkError = 1005,
  kMutexInitError = 1006,
  kMutexLockError = 1007,
  kMutexUnlockError = 1008,
  kMutexTimedlockTimeout = 1009,
  kMutexTimedlockError = 1010,
};

/**
 * @class Semaphore
 * @brief 命名 POSIX 信号量的 RAII 包装
 * 
 * 支持自动创建、等待（带超时）和销毁。
 * 线程安全，异常安全。
 */
class Semaphore {
 public:
  /**
   * @brief 构造函数：创建或打开命名信号量
   * 
   * @param name 信号量名称（格式：/sem_name，必须以 / 开头）
   * @param initial_value 初始值（创建时使用），默认 0
   * @param create_if_not_exists 如果不存在则创建，默认 true
   * @throws std::system_error 如果创建失败
   */
  Semaphore(const std::string& name, unsigned int initial_value = 0,
            bool create_if_not_exists = true);

  /**
   * @brief 析构函数：关闭信号量
   * 不自动删除命名信号量（需手动调用 unlink）
   */
  ~Semaphore() noexcept;

  // 禁用拷贝
  Semaphore(const Semaphore&) = delete;
  Semaphore& operator=(const Semaphore&) = delete;

  // 允许移动
  Semaphore(Semaphore&& other) noexcept;
  Semaphore& operator=(Semaphore&& other) noexcept;

  /**
   * @brief 等待信号量（阻塞）
   * 
   * @return ErrorCode::kSuccess 成功
   */
  ErrorCode Wait();

  /**
   * @brief 等待信号量（带超时）
   * 
   * @param timeout_ms 超时时间（毫秒），0 表示非阻塞
   * @return ErrorCode::kSuccess 成功获取
   * @return ErrorCode::kSemaphoreWaitTimeout 超时
   * @return ErrorCode::kSemaphoreWaitError 其他错误
   */
  ErrorCode WaitTimeout(unsigned int timeout_ms);

  /**
   * @brief 尝试等待（非阻塞）
   * 
   * @return true 成功获取，false 信号量为 0
   */
  bool TryWait();

  /**
   * @brief 发送信号
   * 
   * @return ErrorCode::kSuccess 成功
   */
  ErrorCode Post();

  /**
   * @brief 获取当前值
   * 
   * @return 信号量当前值
   */
  int GetValue() const;

  /**
   * @brief 删除命名信号量（系统级资源）
   * 
   * @return ErrorCode::kSuccess 成功
   */
  static ErrorCode Unlink(const std::string& name);

  /**
   * @brief 获取信号量名称
   */
  const std::string& GetName() const { return name_; }

  /**
   * @brief 检查信号量是否有效
   */
  bool IsValid() const { return sem_ != SEM_FAILED; }

 private:
  std::string name_;
  sem_t* sem_;
};

/**
 * @class Mutex
 * @brief POSIX Mutex 的 RAII 包装
 * 
 * 支持普通锁和递归锁，带超时等待。
 * 线程安全，异常安全。
 */
class Mutex {
 public:
  /**
   * @brief 构造函数：初始化互斥锁
   * 
   * @param is_recursive 是否为递归锁，默认 false
   * @throws std::system_error 如果初始化失败
   */
  explicit Mutex(bool is_recursive = false);

  /**
   * @brief 析构函数：销毁互斥锁
   */
  ~Mutex() noexcept;

  // 禁用拷贝和移动
  Mutex(const Mutex&) = delete;
  Mutex& operator=(const Mutex&) = delete;
  Mutex(Mutex&&) = delete;
  Mutex& operator=(Mutex&&) = delete;

  /**
   * @brief 锁定互斥锁（阻塞）
   * 
   * @return ErrorCode::kSuccess 成功
   */
  ErrorCode Lock();

  /**
   * @brief 尝试锁定（非阻塞）
   * 
   * @return true 成功获取，false 已被锁定
   */
  bool TryLock();

  /**
   * @brief 锁定互斥锁（带超时）
   * 
   * @param timeout_ms 超时时间（毫秒）
   * @return ErrorCode::kSuccess 成功获取
   * @return ErrorCode::kMutexTimedlockTimeout 超时
   * @return ErrorCode::kMutexTimedlockError 其他错误
   */
  ErrorCode LockTimeout(unsigned int timeout_ms);

  /**
   * @brief 解锁互斥锁
   * 
   * @return ErrorCode::kSuccess 成功
   */
  ErrorCode Unlock();

  /**
   * @brief 获取原始 pthread_mutex_t 指针
   * 用于与条件变量等配合使用
   */
  pthread_mutex_t* GetNative() { return &mutex_; }

 private:
  pthread_mutex_t mutex_;
};

/**
 * @class ScopedLock
 * @brief 自动锁守卫（RAII）
 * 
 * 构造时加锁，析构时自动解锁。
 * 支持移动，禁用拷贝。
 */
class ScopedLock {
 public:
  /**
   * @brief 构造函数：尝试加锁
   * 
   * @param mutex 互斥锁引用
   * @param timeout_ms 超时时间（毫秒），0 = 无超时
   * @throws std::system_error 如果加锁失败
   */
  explicit ScopedLock(Mutex& mutex, unsigned int timeout_ms = 0);

  /**
   * @brief 析构函数：自动解锁
   */
  ~ScopedLock() noexcept;

  // 禁用拷贝
  ScopedLock(const ScopedLock&) = delete;
  ScopedLock& operator=(const ScopedLock&) = delete;

  // 允许移动
  ScopedLock(ScopedLock&& other) noexcept;
  ScopedLock& operator=(ScopedLock&& other) noexcept;

  /**
   * @brief 检查是否已获取锁
   */
  bool IsLocked() const { return is_locked_; }

  /**
   * @brief 提前释放锁
   */
  ErrorCode Unlock();

 private:
  Mutex* mutex_;
  bool is_locked_;
};

/**
 * @brief 获取错误码对应的错误消息
 */
std::string ErrorCodeToString(ErrorCode code);

}  // namespace nora::coms::ipc
