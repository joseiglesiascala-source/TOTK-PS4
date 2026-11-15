// PS4 runtime fixes - BEAST MODE (Performance Optimized for TOTK) - BUGFIXED
// Seguridad sacrificada por rendimiento bruto.

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>

#include <atomic>
#include <sys/types.h>

extern "C" {
int32_t sceKernelDebugOutText(int32_t channel, const char* fmt, ...);
int32_t sceKernelAvailableFlexibleMemorySize(size_t* size);
int32_t sceKernelReserveVirtualRange(void** addr, size_t len, int32_t flags, size_t alignment);
int32_t sceKernelMunmap(void* addr, size_t len);
int32_t sceKernelMapNamedFlexibleMemory(void** addr, size_t len, int32_t prot, int32_t flags, const char* name);
int32_t sceKernelMapNamedSystemFlexibleMemory(void** addr, size_t len, int32_t prot, int32_t flags,
                                              const char* name);
size_t sceKernelGetDirectMemorySize(void);
int32_t sceKernelAllocateDirectMemory(off_t start, off_t end, size_t len, size_t align, int32_t type,
                                      off_t* physOut);
int32_t sceKernelMapDirectMemory(void** addr, size_t len, int32_t prot, int32_t flags, off_t phys,
                                 size_t align);
int32_t sceKernelReleaseDirectMemory(off_t start, size_t len);

typedef void* mspace;
mspace create_mspace_with_base(void* base, size_t capacity, int locked);
void* mspace_malloc(mspace msp, size_t bytes);
void mspace_free(mspace msp, void* mem);
void* mspace_calloc(mspace msp, size_t n_elements, size_t elem_size);
void* mspace_realloc(mspace msp, void* mem, size_t newsize);
void* mspace_memalign(mspace msp, size_t alignment, size_t bytes);
size_t mspace_usable_size(const void* mem);
size_t mspace_footprint(mspace msp);
size_t mspace_max_footprint(mspace msp);
void* memcpy(void* dst, const void* src, size_t n);

__attribute__((weak)) void ps4_boot_trace(const char* stage);
int ps4_heap_ok(void* msp); 

extern char __text_start[]; 
}

namespace {

const size_t MB = 1024 * 1024;
const size_t PAGE = 16 * 1024;
const size_t RESERVE_FOR_SYSTEM = 320 * MB;
const int32_t PROT_CPU_RW = 0x3;
const int32_t MAP_FIXED_FLAG = 0x10;

mspace g_heap = nullptr;
const char* g_heapPool = "none";
uintptr_t g_heapBase = 0;
size_t g_heapSize = 0;

__attribute__((format(printf, 1, 2))) void heapLog(const char* fmt, ...) {
    char line[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    sceKernelDebugOutText(0, line);
    if (ps4_boot_trace)
        ps4_boot_trace(line);
}

// Suprimimos el rastreo de pila para ahorrar CPU
void logStack() { }

mspace createMspace(const char* pool, void* base, size_t size) {
    mspace msp = create_mspace_with_base(base, size, 1);
    if (!msp) return nullptr;
    void* test = mspace_malloc(msp, 4096);
    if (!test) return nullptr;
    mspace_free(msp, test);
    g_heapPool = pool;
    g_heapBase = reinterpret_cast<uintptr_t>(base);
    g_heapSize = size;
    heapLog("[totk] heap: dlmalloc on %zu MiB of %s at %p\n", size / MB, pool, base);
    return msp;
}

void* reserveAwayFromKernel(size_t size) {
    const uintptr_t hints[] = {0x1000000000ull, 0x800000000ull, 0x600000000ull};
    for (uintptr_t hint : hints) {
        void* address = reinterpret_cast<void*>(hint);
        if (sceKernelReserveVirtualRange(&address, size, 0, PAGE) == 0) {
            if (reinterpret_cast<uintptr_t>(address) >= 0x400000000ull)
                return address;
        }
    }
    return nullptr;
}

mspace mapAndCreate(bool systemPool, size_t size) {
    void* guard = reserveAwayFromKernel(size + PAGE);
    if (!guard) return nullptr;
    void* base = static_cast<char*>(guard) + PAGE;
    int32_t ret = systemPool
                      ? sceKernelMapNamedSystemFlexibleMemory(&base, size, PROT_CPU_RW, MAP_FIXED_FLAG, "smw heap")
                      : sceKernelMapNamedFlexibleMemory(&base, size, PROT_CPU_RW, MAP_FIXED_FLAG, "smw heap");
    if (ret != 0) return nullptr;
    return createMspace(systemPool ? "system flexible memory" : "flexible memory", base, size);
}

mspace createDirectHeap(size_t size) {
    off_t phys = 0;
    if (sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), size, 2 * MB, 0, &phys) != 0)
        return nullptr;
    void* base = nullptr;
    if (sceKernelMapDirectMemory(&base, size, PROT_CPU_RW, 0, phys, 2 * MB) != 0)
        return nullptr;
    return createMspace("direct memory", base, size);
}

mspace createHeap() {
    size_t available = 0;
    int32_t ret = sceKernelAvailableFlexibleMemorySize(&available);
    
    const size_t systemSizes[] = {256 * MB, 192 * MB, 128 * MB};
    for (size_t size : systemSizes) {
        if (mspace msp = mapAndCreate(true, size))
            return msp;
    }
    size_t size = 0;
    if (ret == 0 && available > RESERVE_FOR_SYSTEM + 32 * MB)
        size = (available - RESERVE_FOR_SYSTEM) & ~(PAGE - 1);
    for (; size >= 32 * MB; size = (size / 2) & ~(PAGE - 1)) {
        if (mspace msp = mapAndCreate(false, size))
            return msp;
    }
    const size_t directSizes[] = {256 * MB, 128 * MB};
    for (size_t direct : directSizes) {
        if (mspace msp = createDirectHeap(direct))
            return msp;
    }
    heapLog("[totk] heap: could not create a heap, out of memory\n");
    return nullptr;
}

bool g_creatingHeap = false;

inline mspace getHeap() {
    if (g_heap == nullptr && !g_creatingHeap) {
        g_creatingHeap = true;
        g_heap = createHeap();
        g_creatingHeap = false;
    }
    return g_heap;
}

constexpr int MAX_OVERFLOW = 64;
constexpr size_t OVERFLOW_SEGMENT = 64 * MB;
struct Overflow {
    uintptr_t base;
    size_t size;
    mspace msp;
};
Overflow g_overflows[MAX_OVERFLOW];
std::atomic<int> g_overflowCount{0};  
size_t g_overflowBytes = 0;
pthread_mutex_t g_overflowLock = PTHREAD_MUTEX_INITIALIZER;

bool mapTopDown(uintptr_t base, size_t size) {
    const size_t total = sceKernelGetDirectMemorySize();
    struct Piece { off_t phys; size_t size; } pieces[256];
    int count = 0;
    size_t done = 0;
    bool ok = true;
    while (ok && done < size) {
        size_t piece = size - done < 16 * MB ? size - done : 16 * MB;
        off_t phys = 0;
        bool got = false;
        for (; piece >= 2 * MB && !got && count < 256; piece = (piece / 2) & ~(2 * MB - 1)) {
            for (size_t start = (total - piece) & ~(2 * MB - 1);; start -= 32 * MB) {
                if (sceKernelAllocateDirectMemory(off_t(start), off_t(total), piece, 2 * MB, 0, &phys) == 0) {
                    got = true;
                    break;
                }
                if (start < 32 * MB) break;
            }
            if (got) break;
        }
        void* at = reinterpret_cast<void*>(base + done);
        if (got) sceKernelMunmap(at, piece); 
        if (!got || sceKernelMapDirectMemory(&at, piece, PROT_CPU_RW, MAP_FIXED_FLAG, phys, PAGE) != 0 ||
            at != reinterpret_cast<void*>(base + done)) {
            if (got) sceKernelReleaseDirectMemory(phys, piece);
            ok = false;
            break;
        }
        pieces[count++] = {phys, piece};
        done += piece;
    }
    if (ok) return true;
    sceKernelMunmap(reinterpret_cast<void*>(base), size);
    for (int i = 0; i < count; i++) sceKernelReleaseDirectMemory(pieces[i].phys, pieces[i].size);
    return false;
}

bool addOverflow(size_t need) {
    const int n = g_overflowCount.load(std::memory_order_relaxed);
    if (n >= MAX_OVERFLOW) return false;
    size_t size = OVERFLOW_SEGMENT;
    if (need + 2 * MB > size) size = (need + 4 * MB) & ~(2 * MB - 1);
    
    void* base = reinterpret_cast<void*>(0x1800000000ull);
    if (sceKernelReserveVirtualRange(&base, size, 0, 2 * MB) != 0 || reinterpret_cast<uintptr_t>(base) < 0x400000000ull) {
        return false;
    }
    if (!mapTopDown(reinterpret_cast<uintptr_t>(base), size)) {
        return false;
    }
    mspace msp = create_mspace_with_base(base, size, 1);
    if (!msp) return false;
    g_overflows[n] = {reinterpret_cast<uintptr_t>(base), size, msp};
    g_overflowBytes += size;
    g_overflowCount.store(n + 1, std::memory_order_release);
    return true;
}

template <class Alloc>
void* fromOverflow(size_t need, Alloc alloc) {
    const int seen = g_overflowCount.load(std::memory_order_acquire);
    for (int i = seen; i-- > 0;)
        if (void* p = alloc(g_overflows[i].msp)) return p;
    pthread_mutex_lock(&g_overflowLock);
    void* p = nullptr;
    for (int i = g_overflowCount.load(std::memory_order_acquire); i-- > seen && !p;)  
        p = alloc(g_overflows[i].msp);
    if (!p && addOverflow(need))
        p = alloc(g_overflows[g_overflowCount.load(std::memory_order_relaxed) - 1].msp);
    pthread_mutex_unlock(&g_overflowLock);
    return p;
}

// --- OPTIMIZACIÓN: Ignoramos el seguimiento de memoria ---
inline void* counted(void* p) { return p; }
inline void uncount(const void* p) { (void)p; }

bool ownsPointer(const void* p) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(p);
    // Chequeo rápido principal (99% de los casos)
    if (address >= g_heapBase && address < g_heapBase + g_heapSize)
        return true;
    
    // Chequeo en overflow (memoria extra)
    const int count = g_overflowCount.load(std::memory_order_relaxed);
    for (int i = count; i-- > 0;) {
        if (address >= g_overflows[i].base && address < g_overflows[i].base + g_overflows[i].size)
            return true;
    }
    return false;
}

__attribute__((tls_model("initial-exec"))) thread_local const char* t_op = "?";
__attribute__((tls_model("initial-exec"))) thread_local const void* t_caller = nullptr;
int g_errors = 0;

// --- OPTIMIZACIÓN CORREGIDA: Arenas de 16MB para evitar Out Of Memory ---
constexpr size_t ARENA_SIZE = 16 * MB; 
constexpr int MAX_ARENAS = 12;
__attribute__((tls_model("initial-exec"))) thread_local mspace t_arena = nullptr;
std::atomic<int> g_arenaCount{0};
mspace g_freeArenas[MAX_ARENAS];
int g_freeArenaCount = 0;
std::atomic_flag g_freeArenaLock = ATOMIC_FLAG_INIT;
pthread_key_t g_arenaKey;
std::atomic<bool> g_arenaKeyReady{false};

void lockFreeArenas() {
    while (g_freeArenaLock.test_and_set(std::memory_order_acquire))
        sched_yield();
}
void unlockFreeArenas() { g_freeArenaLock.clear(std::memory_order_release); }

void releaseArena(void* arena) {
    if (!arena) return;
    lockFreeArenas();
    if (g_freeArenaCount < MAX_ARENAS)
        g_freeArenas[g_freeArenaCount++] = static_cast<mspace>(arena);
    unlockFreeArenas();
}

// --- OPTIMIZACIÓN: Comprobación del Heap desactivada ---
void checkHeap(const char* op, const void* caller) { (void)op; (void)caller; }
void reportFailure(const char* what, size_t size) { (void)what; (void)size; }

struct ThreadDtor {
    void (*dtor)(void*);
    void* obj;
    ThreadDtor* next;
};
pthread_key_t g_threadDtorKey;
pthread_once_t g_threadDtorOnce = PTHREAD_ONCE_INIT;

void runThreadDtors(void* p) {
    auto* head = static_cast<ThreadDtor*>(p);
    while (head) {
        ThreadDtor* next = head->next;
        head->dtor(head->obj);
        free(head);
        head = next;
    }
}

void createThreadDtorKey() { pthread_key_create(&g_threadDtorKey, runThreadDtors); }

}  // namespace

extern "C" {

void ps4_heap_error(void* msp, void* chunk, int corruption) {
    // Si la memoria está corrupta, abortamos para evitar un crash más grave
    (void)msp; (void)chunk;
    if (corruption) abort();
}

void* __wrap_malloc(size_t size) {
    t_op = "malloc";
    t_caller = __builtin_return_address(0);
    checkHeap(t_op, t_caller);
    void* p = t_arena ? mspace_malloc(t_arena, size) : nullptr;
    if (!p) p = mspace_malloc(getHeap(), size);
    if (!p) p = fromOverflow(size, [size](mspace m) { return mspace_malloc(m, size); });
    return counted(p);
}

void __wrap_free(void* ptr) {
    if (!ptr) return;
    t_op = "free";
    t_caller = __builtin_return_address(0);
    checkHeap(t_op, t_caller);
    if (!ownsPointer(ptr)) return;
    uncount(ptr);
    mspace_free(getHeap(), ptr);
}

void* __wrap_calloc(size_t nelem, size_t size) {
    t_op = "calloc";
    t_caller = __builtin_return_address(0);
    checkHeap(t_op, t_caller);
    void* p = t_arena ? mspace_calloc(t_arena, nelem, size) : nullptr;
    if (!p) p = mspace_calloc(getHeap(), nelem, size);
    if (!p) p = fromOverflow(nelem * size, [nelem, size](mspace m) { return mspace_calloc(m, nelem, size); });
    return counted(p);
}

void* __wrap_realloc(void* ptr, size_t size) {
    t_op = "realloc";
    t_caller = __builtin_return_address(0);
    checkHeap(t_op, t_caller);
    if (ptr && !ownsPointer(ptr)) return nullptr;
    
    if (ptr && size == 0) {
        uncount(ptr);
        mspace_free(getHeap(), ptr);
        return nullptr;
    }
    
    const size_t old_size = ptr ? mspace_usable_size(ptr) : 0;
    void* p = ptr ? mspace_realloc(getHeap(), ptr, size) : nullptr;
    if (p) return p;
    
    p = t_arena ? mspace_malloc(t_arena, size) : nullptr;
    if (!p) p = mspace_malloc(getHeap(), size);
    if (!p) p = fromOverflow(size, [size](mspace m) { return mspace_malloc(m, size); });
    if (p && ptr) {
        memcpy(p, ptr, old_size < size ? old_size : size);
        uncount(ptr);
        mspace_free(getHeap(), ptr);
    }
    return counted(p);
}

void* __wrap_memalign(size_t alignment, size_t size) {
    t_op = "memalign";
    t_caller = __builtin_return_address(0);
    checkHeap(t_op, t_caller);
    void* p = t_arena ? mspace_memalign(t_arena, alignment, size) : nullptr;
    if (!p) p = mspace_memalign(getHeap(), alignment, size);
    if (!p) p = fromOverflow(size + alignment, [alignment, size](mspace m) { return mspace_memalign(m, alignment, size); });
    return counted(p);
}

void ps4_heap_private_arena(const char* name) {
    static const char* const busy[] = {"CPU thread", "CPU-GPU thread", "Video thread",
                                       "VK submission thread", "AsyncShaderCompiler Worker",
                                       "DVD thread", "Audio thread - PS4", "nvn replay",
                                       "nvn janitor", "nvn worker 0", "nvn worker 1",
                                       "nvn worker 2"};
    if (t_arena || !name || !getHeap()) return;
    bool wanted = false;
    for (const char* b : busy) wanted |= strcmp(name, b) == 0;
    if (!wanted) return;
    if (!g_arenaKeyReady.load(std::memory_order_acquire)) {
        static std::atomic_flag s_creating = ATOMIC_FLAG_INIT;
        if (!s_creating.test_and_set()) {
            pthread_key_create(&g_arenaKey, releaseArena);
            g_arenaKeyReady.store(true, std::memory_order_release);
        }
        while (!g_arenaKeyReady.load(std::memory_order_acquire)) sched_yield();
    }
    lockFreeArenas();
    if (g_freeArenaCount > 0) t_arena = g_freeArenas[--g_freeArenaCount];
    unlockFreeArenas();
    if (t_arena) {
        pthread_setspecific(g_arenaKey, t_arena);
        return;
    }
    if (g_arenaCount.fetch_add(1) >= MAX_ARENAS) return;
    
    // CORRECCIÓN: 4 * 16MB = 64MB. Suficiente para el hilo principal y el replay sin agotar la RAM.
    const size_t size = (strcmp(name, "nvn replay") == 0 || strcmp(name, "CPU thread") == 0) ? 4 * ARENA_SIZE : ARENA_SIZE;
    
    void* block = mspace_malloc(getHeap(), size);
    if (!block) return;
    t_arena = create_mspace_with_base(block, size, 1);
    if (t_arena) pthread_setspecific(g_arenaKey, t_arena);
}

void* __wrap___memalign(size_t alignment, size_t size) { return __wrap_memalign(alignment, size); }

int ps4_replace_malloc_init(void) { getHeap(); return 0; }
int ps4_replace_malloc_finalize(void) { return 0; }
void* ps4_replace_malloc(size_t size) { return __wrap_malloc(size); }
void ps4_replace_free(void* ptr) { __wrap_free(ptr); }
void* ps4_replace_calloc(size_t nelem, size_t size) { return __wrap_calloc(nelem, size); }
void* ps4_replace_realloc(void* ptr, size_t size) { return __wrap_realloc(ptr, size); }
void* ps4_replace_memalign(size_t boundary, size_t size) { return __wrap_memalign(boundary, size); }
void* ps4_replace_reallocalign(void* ptr, size_t size, size_t boundary) {
    if (!ptr) return __wrap_memalign(boundary, size);
    if (size == 0) { __wrap_free(ptr); return nullptr; }
    void* p = __wrap_memalign(boundary, size);
    if (!p) return nullptr;
    const size_t old_size = mspace_usable_size(ptr);
    memcpy(p, ptr, old_size < size ? old_size : size);
    __wrap_free(ptr);
    return p;
}
int ps4_replace_posix_memalign(void** out, size_t boundary, size_t size) {
    void* p = __wrap_memalign(boundary, size);
    if (!p) return 12;
    *out = p;
    return 0;
}

struct MallocManagedSize {
    uint16_t size;
    uint16_t version;
    uint32_t reserved;
    size_t max_system_size, current_system_size, max_inuse_size, current_inuse_size;
};
int ps4_replace_malloc_stats(void* out) {
    auto* stats = static_cast<MallocManagedSize*>(out);
    if (!stats) return 0x80020016;
    const size_t footprint = mspace_footprint(getHeap());
    stats->max_system_size = mspace_max_footprint(getHeap());
    stats->current_system_size = footprint;
    stats->max_inuse_size = footprint;
    stats->current_inuse_size = footprint;
    return 0;
}
int ps4_replace_malloc_stats_fast(void* out) { return ps4_replace_malloc_stats(out); }
size_t ps4_replace_malloc_usable_size(void* ptr) { return ptr ? mspace_usable_size(ptr) : 0; }

int ps4_jit_in_system_pool = 1;           
unsigned long long ps4_fault_count = 0;   

void* __real_mmap(void* addr, size_t len, int prot, int flags, int fd, off_t offset);
void* __wrap_mmap(void* addr, size_t len, int prot, int flags, int fd, off_t offset) {
    const bool anonymous = fd == -1 && (flags & MAP_ANON) && !(flags & MAP_FIXED);
    const bool allowed = !(prot & PROT_EXEC) || ps4_jit_in_system_pool;
    if (anonymous && allowed && len >= MB) {
        const size_t size = (len + PAGE - 1) & ~(PAGE - 1);
        void* base = reserveAwayFromKernel(size);
        if (base) {
            const int32_t ret = sceKernelMapNamedSystemFlexibleMemory(&base, size, prot & 0x7, MAP_FIXED_FLAG, "smw mmap");
            if (ret == 0) return base;
            sceKernelMunmap(base, size);
        }
    }
    return __real_mmap(addr, len, prot, flags, fd, offset);
}

int __real_clock_gettime(clockid_t id, struct timespec* ts);
uint64_t sceKernelGetTscFrequency_ps4() __asm__("sceKernelGetTscFrequency");

int __wrap_clock_gettime(clockid_t id, struct timespec* ts) {
    if (id != CLOCK_MONOTONIC) return __real_clock_gettime(id, ts);
    struct Anchor {
        uint64_t tsc = 0, ns = 0, mult = 0; 
        bool ok = false;
    };
    static const Anchor anchor = [] {
        Anchor a;
        const uint64_t freq = static_cast<uint64_t>(sceKernelGetTscFrequency_ps4());
        struct timespec now;
        if (freq < 1000000 || __real_clock_gettime(CLOCK_MONOTONIC, &now) != 0) return a;
        a.tsc = __builtin_ia32_rdtsc();
        a.ns = static_cast<uint64_t>(now.tv_sec) * 1000000000ull + static_cast<uint64_t>(now.tv_nsec);
        a.mult = (1000000000ull << 32) / freq;
        a.ok = true;
        return a;
    }();
    if (!anchor.ok) return __real_clock_gettime(id, ts);
    const uint64_t ticks = __builtin_ia32_rdtsc() - anchor.tsc;
    const uint64_t ns = anchor.ns + static_cast<uint64_t>((static_cast<unsigned __int128>(ticks) * anchor.mult) >> 32);
    ts->tv_sec = static_cast<time_t>(ns / 1000000000ull);
    ts->tv_nsec = static_cast<long>(ns % 1000000000ull);
    return 0;
}

int __wrap_pthread_once(pthread_once_t* once, void (*init)(void)) {
    auto* state = reinterpret_cast<int*>(once);
    if (__atomic_load_n(state, __ATOMIC_ACQUIRE) == 2) return 0;
    int expected = 0;
    if (__atomic_compare_exchange_n(state, &expected, 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        init();
        __atomic_store_n(state, 2, __ATOMIC_RELEASE);
        return 0;
    }
    while (__atomic_load_n(state, __ATOMIC_ACQUIRE) != 2) sched_yield();
    return 0;
}

// --- OPTIMIZACIÓN: Eliminado el temporizador de 50ms ---
extern "C" int __real_pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex);
extern "C" int __wrap_pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
    return __real_pthread_cond_wait(cond, mutex);
}

extern "C" int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg);
extern "C" void* scePthreadSelf();
extern "C" void scePthreadSetaffinity();  
extern "C" void scePthreadGetaffinity();  
extern "C" void ps4_boot_trace(const char* stage);
extern "C" void ps4_watch_thread(const char* name);  
extern "C" void ps4_install_signal_stack();        

extern "C" {
uint64_t ps4_new_thread_cores = 0x3F;
}

namespace {
// CORRECCIÓN: Volvemos a 4MB de stack. 2MB es muy poco y causará crashes con los shaders de TOTK.
constexpr size_t kMinThreadStack = 4 * 1024 * 1024;
using SetAffinityFn = int (*)(void*, uint64_t);
using GetAffinityFn = int (*)(void*, uint64_t*);

struct ThreadStart {
    void* (*start)(void*);
    void* arg;
};

void* startOnAppCores(void* raw) {
    const ThreadStart start = *static_cast<ThreadStart*>(raw);
    free(raw);
    reinterpret_cast<SetAffinityFn>(&scePthreadSetaffinity)(scePthreadSelf(), ps4_new_thread_cores);
    ps4_install_signal_stack();
    ps4_watch_thread("(unnamed)");
    return start.start(start.arg);
}
}  // namespace

extern "C" int scePthreadSetprio(void* thread, int prio);
extern "C" void ps4_boost_thread() {
    scePthreadSetprio(scePthreadSelf(), 650);
}

int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg) {
    pthread_attr_t local;
    bool ownAttr = false;
    size_t stack = 0;
    if (attr) {
        pthread_attr_getstacksize(attr, &stack);
        if (stack < kMinThreadStack)
            pthread_attr_setstacksize(const_cast<pthread_attr_t*>(attr), kMinThreadStack);
    } else if (pthread_attr_init(&local) == 0) {
        ownAttr = true;
        pthread_attr_getstacksize(&local, &stack);
        if (stack < kMinThreadStack)
            pthread_attr_setstacksize(&local, kMinThreadStack);
        attr = &local;
    }
    
    int result;
    auto* wrapped = static_cast<ThreadStart*>(malloc(sizeof(ThreadStart)));
    if (!wrapped) {
        result = __real_pthread_create(thread, attr, start, arg);
    } else {
        *wrapped = {start, arg};
        result = __real_pthread_create(thread, attr, startOnAppCores, wrapped);
        if (result != 0) free(wrapped);
    }
    if (ownAttr) pthread_attr_destroy(&local);
    return result;
}

int __cxa_thread_atexit_impl(void (*dtor)(void*), void* obj, void* /*dso_symbol*/) {
    pthread_once(&g_threadDtorOnce, createThreadDtorKey);
    auto* entry = static_cast<ThreadDtor*>(malloc(sizeof(ThreadDtor)));
    if (!entry) return -1;
    entry->dtor = dtor;
    entry->obj = obj;
    entry->next = static_cast<ThreadDtor*>(pthread_getspecific(g_threadDtorKey));
    pthread_setspecific(g_threadDtorKey, entry);
    return 0;
}

void ps4_heap_usage(uint64_t* in_use, uint64_t* main_size, uint64_t* overflow_bytes, uint32_t* overflow_segments) {
    *in_use = 0; 
    *main_size = g_heapSize;
    pthread_mutex_lock(&g_overflowLock);
    *overflow_bytes = g_overflowBytes;
    *overflow_segments = uint32_t(g_overflowCount.load(std::memory_order_relaxed));
    pthread_mutex_unlock(&g_overflowLock);
}

int ps4_heap_top(char* out, size_t size) {
    (void)out; (void)size;
    return 0; 
}

const char* ps4rt_heap_source(void) {
    getHeap();
    return g_heapPool;
}
size_t ps4rt_heap_size(void) {
    getHeap();
    return g_heapSize;
}

}  // extern "C"