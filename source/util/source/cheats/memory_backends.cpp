#include <onion/log.h>
#include "cheats/i_memory_backend.hpp"

#include <cerrno>
#include <cstring>
#include <sys/mman.h>

#include <ps5/kernel.h>
#include <ps5/mdbg.h>

#include "pt.h"
#include "util_platform.h"


namespace onion::cheats {
namespace {

constexpr uint64_t kPageSize = 0x4000ULL;

inline uint64_t pageAlignDown(uint64_t v) {
  return v & ~(kPageSize - 1ULL);
}
inline uint64_t pageAlignUp(uint64_t v) {
  return (v + kPageSize - 1ULL) & ~(kPageSize - 1ULL);
}

int mapCodeCaveCommon(pid_t pid, uint64_t addr, size_t len) {
  const uint64_t page_start = pageAlignDown(addr);
  const uint64_t page_end = pageAlignUp(addr + len);
  const size_t page_len = static_cast<size_t>(page_end - page_start);

  if (pt_attach_proc(pid) < 0) {
    LOG_ERROR("[Cheat] code cave attach failed pid=%d errno=%d", pid,
                 errno);
    return -1;
  }

  const intptr_t mapped =
      pt_mmap(pid, static_cast<intptr_t>(page_start), page_len,
              PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
  if (mapped != static_cast<intptr_t>(page_start)) {
    LOG_ERROR("[Cheat] code cave mmap failed page=0x%llx ret=0x%llx",
                 (unsigned long long)page_start, (unsigned long long)mapped);
    pt_detach_proc(pid, 0);
    return -1;
  }

  if (kernel_mprotect(pid, page_start, page_len,
                      PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
    LOG_ERROR("[Cheat] code cave mprotect failed page=0x%llx",
                 (unsigned long long)page_start);
    pt_detach_proc(pid, 0);
    return -1;
  }

  pt_detach_proc(pid, 0);
  return 0;
}

class MdbgBackend final : public IMemoryBackend {
public:
  int read(pid_t pid, uint64_t addr, void *buf, size_t len) override {
    return mdbg_copyout(pid, static_cast<intptr_t>(addr), buf, len);
  }
  int write(pid_t pid, uint64_t addr, const void *buf, size_t len) override {
    return mdbg_copyin(pid, buf, static_cast<intptr_t>(addr), len);
  }
  int mapCodeCave(pid_t pid, uint64_t addr, size_t len) override {
    return mapCodeCaveCommon(pid, addr, len);
  }
};

class KdirectBackend final : public IMemoryBackend {
  static constexpr uint64_t kProcVmspace = 0x200;

  static int getProcCr3(pid_t pid, uint64_t *cr3, uint64_t *dmap_base) {
    const uint64_t proc = kernel_get_proc(pid);
    if (proc == 0) {
      return -1;
    }
    uint64_t vmspace = 0;
    if (kernel_copyout(proc + kProcVmspace, &vmspace, sizeof(vmspace)) < 0 ||
        vmspace == 0) {
      return -1;
    }
    const uint32_t fw = util_system_fw_major();
    const uint32_t pmap_off = (fw >= 0x600) ? 0x2E8u : 0x2E0u;
    uint64_t ptrs[2] = {0, 0};
    if (kernel_copyout(vmspace + pmap_off + 32, ptrs, sizeof(ptrs)) < 0) {
      return -1;
    }
    if (cr3) {
      *cr3 = ptrs[1];
    }
    if (dmap_base) {
      *dmap_base = ptrs[0] - ptrs[1];
    }
    return 0;
  }

  static uint64_t virt2phys(uintptr_t addr, uint64_t dmap, uint64_t pml,
                            uint64_t *phys_limit) {
    for (int i = 39; i >= 12; i -= 9) {
      uint64_t inner = 0;
      const uint64_t pte =
          dmap + pml + ((addr & (0x1ffULL << i)) >> (i - 3));
      if (kernel_copyout(pte, &inner, sizeof(inner)) < 0) {
        return static_cast<uint64_t>(-1);
      }
      if (!(inner & 1)) {
        return static_cast<uint64_t>(-1);
      }
      if ((inner & 128) || i == 12) {
        inner &= (1ULL << 52) - (1ULL << i);
        inner |= addr & ((1ULL << i) - 1ULL);
        if (phys_limit) {
          *phys_limit = (inner | ((1ULL << i) - 1ULL)) + 1ULL;
        }
        return inner;
      }
      inner &= (1ULL << 52) - (1ULL << 12);
      pml = inner;
    }
    return static_cast<uint64_t>(-1);
  }

  static int copy(pid_t pid, uint64_t addr, void *buf, size_t len, bool to_proc) {
    uint64_t cr3 = 0, dmap = 0;
    if (getProcCr3(pid, &cr3, &dmap) < 0) {
      return -1;
    }
    auto *p = static_cast<uint8_t *>(buf);
    uint64_t vaddr = addr;
    size_t left = len;
    while (left > 0) {
      uint64_t phys_end = 0;
      const uint64_t phys = virt2phys(vaddr, dmap, cr3, &phys_end);
      if (phys == static_cast<uint64_t>(-1)) {
        return -1;
      }
      size_t chunk = static_cast<size_t>(phys_end - phys);
      if (left < chunk) {
        chunk = left;
      }
      const int rc = to_proc ? kernel_copyin(p, dmap + phys, chunk)
                             : kernel_copyout(dmap + phys, p, chunk);
      if (rc < 0) {
        return -1;
      }
      vaddr += chunk;
      p += chunk;
      left -= chunk;
    }
    return 0;
  }

public:
  int read(pid_t pid, uint64_t addr, void *buf, size_t len) override {
    return copy(pid, addr, buf, len, false);
  }
  int write(pid_t pid, uint64_t addr, const void *buf, size_t len) override {
    return copy(pid, addr, const_cast<void *>(buf), len, true);
  }
  int mapCodeCave(pid_t pid, uint64_t addr, size_t len) override {
    return mapCodeCaveCommon(pid, addr, len);
  }
};

} // namespace

std::unique_ptr<IMemoryBackend> MemoryBackendFactory::create(uint32_t fw_major) {
  if (fw_major >= 0x840) {
    return std::make_unique<KdirectBackend>();
  }
  return std::make_unique<MdbgBackend>();
}

} // namespace onion::cheats
