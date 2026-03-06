/**
 * @file shared_buffer.h
 * @brief 共享内存缓冲区管理
 * 
 * 提供零拷贝的进程间共享内存管理：
 * - SharedMemoryBuffer: 创建和管理共享内存段
 * - 自动 mmap/munmap 和 shmget/shmdt
 * - 线程安全的访问和生命周期管理
 * 
 * @author Nora Project
 * @version 1.0.0
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace nora::coms::ipc {

/**
 * @class SharedMemoryBuffer
 * @brief 共享内存缓冲区
 * 
 * 管理 System V 共享内存段的生命周期。
 * 线程安全（只读访问，创建/销毁需同步）。
 */
class SharedMemoryBuffer {
 public:
  /**
   * @brief 构造函数：创建或附加到共享内存
   * 
   * @param key 共享内存键（使用 ftok() 或直接整数）
   * @param size 缓冲区大小（字节），仅在创建时使用
   * @param create_if_not_exists 如果不存在则创建，默认 true
   * @throws std::system_error 如果操作失败
   * 
   * 示例：
   *   key_t key = ftok("/path/to/project", 'V');
   *   SharedMemoryBuffer buffer(key, 4096);
   *   void* data = buffer.GetData();
   */
  SharedMemoryBuffer(key_t key, size_t size, bool create_if_not_exists = true);

  /**
   * @brief 析构函数：分离共享内存
   * 不销毁共享内存段（需手动调用 Remove）
   */
  ~SharedMemoryBuffer() noexcept;

  // 禁用拷贝
  SharedMemoryBuffer(const SharedMemoryBuffer&) = delete;
  SharedMemoryBuffer& operator=(const SharedMemoryBuffer&) = delete;

  // 允许移动
  SharedMemoryBuffer(SharedMemoryBuffer&& other) noexcept;
  SharedMemoryBuffer& operator=(SharedMemoryBuffer&& other) noexcept;

  /**
   * @brief 获取缓冲区指针
   * 
   * @return 指向共享内存的 void 指针
   */
  void* GetData() const { return data_; }

  /**
   * @brief 获取缓冲区大小
   * 
   * @return 字节数
   */
  size_t GetSize() const { return size_; }

  /**
   * @brief 获取共享内存 ID
   * 
   * @return 系统级的 shmid
   */
  int GetShmid() const { return shmid_; }

  /**
   * @brief 检查缓冲区是否有效
   */
  bool IsValid() const { return data_ != nullptr && shmid_ != -1; }

  /**
   * @brief 删除共享内存段（系统级资源）
   * 
   * @param key 共享内存键
   * @return true 成功，false 失败或不存在
   */
  static bool Remove(key_t key);

  /**
   * @brief 从路径和项目 ID 生成共享内存键
   * 
   * @param path 文件路径（必须存在）
   * @param project_id 项目 ID（0-255）
   * @return 生成的键值
   * @throws std::system_error 如果路径不存在
   */
  static key_t MakeKey(const std::string& path, int project_id);

 private:
  void* data_;
  size_t size_;
  int shmid_;
};

/**
 * @struct CacheAlignedLayout
 * @brief 缓存对齐的内存布局工具
 * 
 * 用于分配缓存行对齐的结构体。
 * L1 缓存线大小：64 字节（x86/ARM）
 */
struct CacheAlignedLayout {
  static constexpr size_t kCacheLineSize = 64;

  /**
   * @brief 对齐大小到缓存行边界
   * 
   * @param size 原始大小
   * @return 对齐后的大小
   */
  static constexpr size_t AlignSize(size_t size) {
    return ((size + kCacheLineSize - 1) / kCacheLineSize) * kCacheLineSize;
  }

  /**
   * @brief 计算对齐偏移量
   * 
   * @param current_offset 当前偏移
   * @return 下一个对齐位置
   */
  static constexpr size_t AlignOffset(size_t current_offset) {
    size_t remainder = current_offset % kCacheLineSize;
    return remainder == 0 ? current_offset : current_offset + (kCacheLineSize - remainder);
  }
};

}  // namespace nora::coms::ipc
