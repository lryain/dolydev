#include "capture_control.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

struct CaptureControlHeader {
    uint32_t version;
    uint32_t playback_count;
    uint32_t is_playing; // 0 = false, 1 = true
};

static const char* default_name = "/doly_capture_control";
static int shm_fd = -1;
static CaptureControlHeader* header = nullptr;
static size_t mapped_size = 0;

bool capture_control_open(const char* name, bool create) {
    const char* shm_name = name ? name : default_name;
    if (shm_fd != -1) {
        // already open
        return true;
    }
    int flags = create ? (O_CREAT | O_RDWR) : O_RDWR;
    shm_fd = shm_open(shm_name, flags, 0666);
    if (shm_fd < 0) {
        perror("capture_control: shm_open failed");
        return false;
    }

    size_t size = sizeof(CaptureControlHeader);
    if (create) {
        if (ftruncate(shm_fd, static_cast<off_t>(size)) != 0) {
            perror("capture_control: ftruncate failed");
            ::close(shm_fd);
            shm_fd = -1;
            return false;
        }
    }

    void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (addr == MAP_FAILED) {
        perror("capture_control: mmap failed");
        ::close(shm_fd);
        shm_fd = -1;
        return false;
    }

    header = reinterpret_cast<CaptureControlHeader*>(addr);
    mapped_size = size;
    if (create) {
        __atomic_store_n(&header->version, 1u, __ATOMIC_SEQ_CST);
        __atomic_store_n(&header->playback_count, 0u, __ATOMIC_SEQ_CST);
        __atomic_store_n(&header->is_playing, 0u, __ATOMIC_SEQ_CST);
    }
    return true;
}

void capture_control_close() {
    if (header) {
        munmap(header, mapped_size);
        header = nullptr;
    }
    if (shm_fd >= 0) {
        ::close(shm_fd);
        shm_fd = -1;
    }
}

uint32_t capture_control_inc() {
    if (!header) return 0;
    uint32_t newv = __atomic_add_fetch(&header->playback_count, 1u, __ATOMIC_SEQ_CST);
    return newv;
}

uint32_t capture_control_dec() {
    if (!header) return 0;
    uint32_t oldv = __atomic_sub_fetch(&header->playback_count, 1u, __ATOMIC_SEQ_CST);
    if (oldv > 0u) {
        // ok
    } else {
        // never underflow; ensure zero
        __atomic_store_n(&header->playback_count, 0u, __ATOMIC_SEQ_CST);
        oldv = 0u;
    }
    return oldv;
}

uint32_t capture_control_count() {
    if (!header) return 0;
    return __atomic_load_n(&header->playback_count, __ATOMIC_SEQ_CST);
}

bool capture_control_is_muted() {
    return capture_control_count() > 0u;
}

void capture_control_set_playing(bool playing) {
    if (!header) return;
    __atomic_store_n(&header->is_playing, playing ? 1u : 0u, __ATOMIC_SEQ_CST);
}

bool capture_control_is_playing() {
    if (!header) return false;
    return __atomic_load_n(&header->is_playing, __ATOMIC_SEQ_CST) != 0u;
}
