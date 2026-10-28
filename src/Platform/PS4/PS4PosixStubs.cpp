#if defined(ENGINE_PLATFORM_PS4) || defined(__PS4__) || defined(__ORBIS__)

#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cctype>
#include <cwctype>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <clocale>
#include <cstdarg>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <pthread.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/time.h>
#include <orbis/libkernel.h>

#define STB_SPRINTF_IMPLEMENTATION
#include <stb/stb_sprintf.h>

extern "C" {

extern size_t sceKernelWrite(int32_t fd, const void* buf, size_t nbyte);

// -------------------------------------------------------------
// Direct Debug Output Helpers
// -------------------------------------------------------------
static void ps4_klog(const char* text) {
    if (text) sceKernelDebugOutText(0, text);
}

static void ps4_kprintf(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    stbsp_vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    sceKernelDebugOutText(0, buf);
}

// -------------------------------------------------------------
// PS4 Dedicated Heap Allocator (256 MB Direct Memory)
// -------------------------------------------------------------
static constexpr size_t PS4_HEAP_SIZE = 256 * 1024 * 1024; // 256 MB
static uint8_t* s_heap_start = nullptr;
static uint8_t* s_heap_end = nullptr;
static bool s_heap_initialized = false;
static std::atomic_flag s_heap_lock = ATOMIC_FLAG_INIT;

struct MemBlock {
    size_t size;         // Total size of block including sizeof(MemBlock)
    bool is_free;
    MemBlock* phys_prev; // Physically previous block
    MemBlock* phys_next; // Physically next block
};

static MemBlock* s_first_block = nullptr;

static void heap_acquire() {
    while (s_heap_lock.test_and_set(std::memory_order_acquire)) {
        #if defined(__x86_64__)
        __builtin_ia32_pause();
        #endif
    }
}

static void heap_release() {
    s_heap_lock.clear(std::memory_order_release);
}

static bool init_ps4_heap() {
    if (s_heap_initialized) return true;

    off_t phys_offset = 0;
    size_t direct_size = sceKernelGetDirectMemorySize();
    if (direct_size == 0) direct_size = 0x120000000;

    int32_t rc = sceKernelAllocateDirectMemory(0, direct_size, PS4_HEAP_SIZE, 0x200000, 3, &phys_offset);
    if (rc < 0) {
        ps4_kprintf("[MyEngine Heap FATAL] sceKernelAllocateDirectMemory(256MB) failed: 0x%08x\n", rc);
        return false;
    }

    void* mapped = nullptr;
    rc = sceKernelMapDirectMemory(&mapped, PS4_HEAP_SIZE, 0x33, 0, phys_offset, 0x200000);
    if (rc < 0 || !mapped) {
        ps4_kprintf("[MyEngine Heap FATAL] sceKernelMapDirectMemory failed: 0x%08x\n", rc);
        return false;
    }

    s_heap_start = reinterpret_cast<uint8_t*>(mapped);
    s_heap_end = s_heap_start + PS4_HEAP_SIZE;

    s_first_block = reinterpret_cast<MemBlock*>(s_heap_start);
    s_first_block->size = PS4_HEAP_SIZE;
    s_first_block->is_free = true;
    s_first_block->phys_prev = nullptr;
    s_first_block->phys_next = nullptr;

    s_heap_initialized = true;
    ps4_kprintf("[MyEngine Heap] 256 MB PS4 direct memory heap mapped at %p\n", mapped);
    return true;
}

void* malloc(size_t size) {
    if (size == 0) return nullptr;

    heap_acquire();
    if (!s_heap_initialized) {
        if (!init_ps4_heap()) {
            heap_release();
            return nullptr;
        }
    }

    // Align payload size to 16 bytes
    size_t aligned_payload = (size + 15) & ~15;
    size_t total_needed = sizeof(MemBlock) + aligned_payload;

    MemBlock* curr = s_first_block;
    while (curr) {
        if (curr->is_free && curr->size >= total_needed) {
            // Split block if enough leftover
            if (curr->size >= total_needed + sizeof(MemBlock) + 32) {
                MemBlock* next_block = reinterpret_cast<MemBlock*>(reinterpret_cast<uint8_t*>(curr) + total_needed);
                next_block->size = curr->size - total_needed;
                next_block->is_free = true;
                next_block->phys_prev = curr;
                next_block->phys_next = curr->phys_next;

                if (curr->phys_next) {
                    curr->phys_next->phys_prev = next_block;
                }
                curr->phys_next = next_block;
                curr->size = total_needed;
            }

            curr->is_free = false;
            heap_release();
            return reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(curr) + sizeof(MemBlock));
        }
        curr = curr->phys_next;
    }

    heap_release();
    ps4_kprintf("[MyEngine Heap WARNING] Out of memory requesting %zu bytes!\n", size);
    return nullptr;
}

void free(void* ptr) {
    if (!ptr) return;

    uint8_t* p = reinterpret_cast<uint8_t*>(ptr);
    if (p < s_heap_start || p >= s_heap_end) {
        return; // Not in our heap
    }

    heap_acquire();
    MemBlock* block = reinterpret_cast<MemBlock*>(p - sizeof(MemBlock));
    block->is_free = true;

    // Coalesce with physically next block if free
    if (block->phys_next && block->phys_next->is_free) {
        MemBlock* n = block->phys_next;
        block->size += n->size;
        block->phys_next = n->phys_next;
        if (n->phys_next) {
            n->phys_next->phys_prev = block;
        }
    }

    // Coalesce with physically previous block if free
    if (block->phys_prev && block->phys_prev->is_free) {
        MemBlock* prev = block->phys_prev;
        prev->size += block->size;
        prev->phys_next = block->phys_next;
        if (block->phys_next) {
            block->phys_next->phys_prev = prev;
        }
    }

    heap_release();
}

void* calloc(size_t num, size_t size) {
    size_t total = num * size;
    if (total == 0) return nullptr;
    void* p = malloc(total);
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

void* realloc(void* ptr, size_t new_size) {
    if (!ptr) return malloc(new_size);
    if (new_size == 0) {
        free(ptr);
        return nullptr;
    }

    uint8_t* p = reinterpret_cast<uint8_t*>(ptr);
    if (p < s_heap_start || p >= s_heap_end) {
        return malloc(new_size);
    }

    heap_acquire();
    MemBlock* block = reinterpret_cast<MemBlock*>(p - sizeof(MemBlock));
    size_t old_payload = block->size - sizeof(MemBlock);
    if (new_size <= old_payload) {
        heap_release();
        return ptr;
    }
    heap_release();

    void* new_ptr = malloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, old_payload);
        free(ptr);
    }
    return new_ptr;
}

void* aligned_alloc(size_t alignment, size_t size) {
    return malloc(size);
}

// -------------------------------------------------------------
// Formatted String Functions (backed by stb_sprintf)
// -------------------------------------------------------------
int vsnprintf(char* buf, size_t count, const char* fmt, va_list va) {
    if (!buf && count > 0) return 0;
    return stbsp_vsnprintf(buf, (int)count, fmt, va);
}

int snprintf(char* buf, size_t count, const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    int n = stbsp_vsnprintf(buf, (int)count, fmt, va);
    va_end(va);
    return n;
}

int sprintf(char* buf, const char* fmt, ...) {
    va_list va;
    va_start(va, fmt);
    int n = stbsp_vsprintf(buf, fmt, va);
    va_end(va);
    return n;
}

int vsprintf(char* buf, const char* fmt, va_list va) {
    return stbsp_vsprintf(buf, fmt, va);
}

// -------------------------------------------------------------
// PS4 Standard Streams & File I/O
// -------------------------------------------------------------
struct PS4_FILE {
    int32_t fd;
    int eof;
    int error;
};

static PS4_FILE s_ps4_stdin  = { 0, 0, 0 };
static PS4_FILE s_ps4_stdout = { 1, 0, 0 };
static PS4_FILE s_ps4_stderr = { 2, 0, 0 };

#undef stdin
#undef stdout
#undef stderr
FILE *const stdin  = (FILE*)&s_ps4_stdin;
FILE *const stdout = (FILE*)&s_ps4_stdout;
FILE *const stderr = (FILE*)&s_ps4_stderr;

size_t fwrite(const void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (!ptr || size == 0 || nmemb == 0) return 0;
    if (stream == stdout || stream == stderr || !stream) {
        size_t total = size * nmemb;
        char buf[1024];
        size_t offset = 0;
        while (offset < total) {
            size_t chunk = (total - offset < sizeof(buf) - 1) ? (total - offset) : (sizeof(buf) - 1);
            memcpy(buf, (const char*)ptr + offset, chunk);
            buf[chunk] = '\0';
            ps4_klog(buf);
            offset += chunk;
        }
        return nmemb;
    }
    PS4_FILE* fp = (PS4_FILE*)stream;
    size_t total = size * nmemb;
    size_t written = sceKernelWrite(fp->fd, ptr, total);
    if (written < total) {
        fp->error = 1;
    }
    return written / size;
}

size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream) {
    if (!stream || !ptr || size == 0 || nmemb == 0) return 0;
    PS4_FILE* fp = (PS4_FILE*)stream;
    size_t total = size * nmemb;
    size_t bytes_read = sceKernelRead(fp->fd, ptr, total);
    if (bytes_read == 0 && total > 0) {
        fp->eof = 1;
    } else if ((ssize_t)bytes_read < 0) {
        fp->error = 1;
        return 0;
    }
    return bytes_read / size;
}

int vfprintf(FILE* stream, const char* format, va_list arg) {
    char buf[2048];
    int n = stbsp_vsnprintf(buf, sizeof(buf), format, arg);
    if (stream == stdout || stream == stderr || !stream) {
        ps4_klog(buf);
    } else {
        fwrite(buf, 1, n, stream);
    }
    return n;
}

int fprintf(FILE* stream, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int n = vfprintf(stream, format, args);
    va_end(args);
    return n;
}

int fputc(int c, FILE* stream) {
    char ch = (char)c;
    fwrite(&ch, 1, 1, stream);
    return c;
}

int fputs(const char* s, FILE* stream) {
    if (!s) return 0;
    size_t len = strlen(s);
    fwrite(s, 1, len, stream);
    return (int)len;
}

int fflush(FILE* stream) {
    return 0;
}

int setvbuf(FILE* stream, char* buf, int mode, size_t size) {
    return 0;
}

void setbuf(FILE* stream, char* buf) {
}

int ungetc(int c, FILE* stream) {
    return c;
}

FILE* fopen(const char* filename, const char* mode) {
    if (!filename || !mode) return nullptr;

    int flags = 0;
    if (strchr(mode, '+')) {
        if (strchr(mode, 'w')) {
            flags = 0x0002 | 0x0200 | 0x0400; // O_RDWR | O_CREAT | O_TRUNC
        } else if (strchr(mode, 'a')) {
            flags = 0x0002 | 0x0200 | 0x0008; // O_RDWR | O_CREAT | O_APPEND
        } else {
            flags = 0x0002; // O_RDWR
        }
    } else {
        if (strchr(mode, 'w')) {
            flags = 0x0001 | 0x0200 | 0x0400; // O_WRONLY | O_CREAT | O_TRUNC
        } else if (strchr(mode, 'a')) {
            flags = 0x0001 | 0x0200 | 0x0008; // O_WRONLY | O_CREAT | O_APPEND
        } else {
            flags = 0x0000; // O_RDONLY
        }
    }

    int32_t fd = sceKernelOpen(filename, flags, 0777);
    if (fd < 0 && filename[0] != '/') {
        char app0_path[1024];
        stbsp_snprintf(app0_path, sizeof(app0_path), "/app0/%s", filename);
        fd = sceKernelOpen(app0_path, flags, 0777);
    }

    if (fd < 0) {
        return nullptr;
    }

    PS4_FILE* fp = (PS4_FILE*)malloc(sizeof(PS4_FILE));
    if (!fp) {
        sceKernelClose(fd);
        return nullptr;
    }
    fp->fd = fd;
    fp->eof = 0;
    fp->error = 0;
    return (FILE*)fp;
}

int fclose(FILE* stream) {
    if (!stream || stream == stdout || stream == stderr || stream == stdin) return 0;
    PS4_FILE* fp = (PS4_FILE*)stream;
    int32_t res = sceKernelClose(fp->fd);
    free(fp);
    return res >= 0 ? 0 : -1;
}

int fseek(FILE* stream, long offset, int whence) {
    if (!stream) return -1;
    PS4_FILE* fp = (PS4_FILE*)stream;
    off_t res = sceKernelLseek(fp->fd, (off_t)offset, whence);
    if (res < 0) {
        fp->error = 1;
        return -1;
    }
    fp->eof = 0;
    return 0;
}

long ftell(FILE* stream) {
    if (!stream) return -1;
    PS4_FILE* fp = (PS4_FILE*)stream;
    off_t res = sceKernelLseek(fp->fd, 0, 1 /* SEEK_CUR */);
    return (long)res;
}

void rewind(FILE* stream) {
    fseek(stream, 0, 0 /* SEEK_SET */);
}

int feof(FILE* stream) {
    if (!stream) return 0;
    PS4_FILE* fp = (PS4_FILE*)stream;
    return fp->eof;
}

int ferror(FILE* stream) {
    if (!stream) return 0;
    PS4_FILE* fp = (PS4_FILE*)stream;
    return fp->error;
}

int fileno(FILE* stream) {
    if (!stream) return -1;
    PS4_FILE* fp = (PS4_FILE*)stream;
    return fp->fd;
}

int remove(const char* path) {
    if (!path) return -1;
    return sceKernelUnlink(path);
}

// -------------------------------------------------------------
// Memory, String, and Utility Functions
// -------------------------------------------------------------
void* memchr(const void* s, int c, size_t n) {
    if (!s) return nullptr;
    const unsigned char* p = (const unsigned char*)s;
    unsigned char uc = (unsigned char)c;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] == uc) return (void*)(p + i);
    }
    return nullptr;
}

void* memrchr(const void* s, int c, size_t n) {
    if (!s) return nullptr;
    const unsigned char* p = (const unsigned char*)s;
    unsigned char uc = (unsigned char)c;
    for (size_t i = n; i > 0; --i) {
        if (p[i - 1] == uc) return (void*)(p + i - 1);
    }
    return nullptr;
}

static uint64_t s_rand_seed = 1;
void srand(unsigned int seed) {
    s_rand_seed = seed;
}
int rand(void) {
    s_rand_seed = s_rand_seed * 6364136223846793005ULL + 1;
    return (int)((s_rand_seed >> 33) & 0x7FFFFFFF);
}

int sched_getscheduler(pid_t pid) {
    return 0;
}

int inet_pton(int af, const char* src, void* dst) {
    if (!src || !dst) return 0;
    if (af == 2 /* AF_INET */) {
        unsigned int b[4] = {0};
        int parts = 0;
        const char* p = src;
        while (*p && parts < 4) {
            if (*p < '0' || *p > '9') return 0;
            unsigned int val = 0;
            while (*p >= '0' && *p <= '9') {
                val = val * 10 + (*p - '0');
                if (val > 255) return 0;
                p++;
            }
            b[parts++] = val;
            if (parts < 4) {
                if (*p != '.') return 0;
                p++;
            }
        }
        if (parts != 4 || *p != '\0') return 0;
        uint8_t* out = (uint8_t*)dst;
        out[0] = (uint8_t)b[0];
        out[1] = (uint8_t)b[1];
        out[2] = (uint8_t)b[2];
        out[3] = (uint8_t)b[3];
        return 1;
    }
    return 0;
}

const char* inet_ntop(int af, const void* src, char* dst, unsigned int size) {
    if (!src || !dst) return nullptr;
    if (af == 2 /* AF_INET */) {
        const uint8_t* in = (const uint8_t*)src;
        stbsp_snprintf(dst, (int)size, "%u.%u.%u.%u", in[0], in[1], in[2], in[3]);
        return dst;
    }
    return nullptr;
}

void abort(void) {
    ps4_klog("[MyEngine PS4 FATAL] abort() called!\n");
    sceKernelBacktraceSelf();
    while (1) {
        #if defined(__x86_64__)
        __builtin_ia32_pause();
        #endif
    }
}

void exit(int status) {
    ps4_kprintf("[MyEngine PS4] exit(%d) called\n", status);
    abort();
}

int mbtowc(wchar_t* pwc, const char* s, size_t n) {
    if (!s) return 0;
    if (n == 0) return -1;
    if (pwc) *pwc = (wchar_t)(unsigned char)*s;
    return (*s != 0) ? 1 : 0;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i] || s1[i] == '\0') {
            return (unsigned char)s1[i] - (unsigned char)s2[i];
        }
    }
    return 0;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++)
        dest[i] = src[i];
    for (; i < n; i++)
        dest[i] = '\0';
    return dest;
}

char* strchr(const char* s, int c) {
    while (*s != (char)c) {
        if (!*s++) return nullptr;
    }
    return const_cast<char*>(s);
}

char* strrchr(const char* s, int c) {
    const char* last = nullptr;
    do {
        if (*s == (char)c) last = s;
    } while (*s++);
    return const_cast<char*>(last);
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return const_cast<char*>(haystack);
    for (; *haystack; haystack++) {
        if (*haystack == *needle) {
            const char* h = haystack;
            const char* n = needle;
            while (*h && *n && *h == *n) { h++; n++; }
            if (!*n) return const_cast<char*>(haystack);
        }
    }
    return nullptr;
}

// Process Parameters and errno
static thread_local int s_ps4_errno = 0;
int* __errno_location() {
    return &s_ps4_errno;
}

// Temporary file stubs
FILE* tmpfile(void) {
    return nullptr;
}

char* tmpnam(char* s) {
    return nullptr;
}

char* getenv(const char* name) {
    return nullptr;
}

void* opendir(const char* name) { return nullptr; }
void* fdopendir(int fd) { return nullptr; }
void* readdir(void* dirp) { return nullptr; }
int closedir(void* dirp) { return 0; }

char* getcwd(char* buf, size_t size) {
    if (buf && size > 6) {
        strcpy(buf, "/app0");
        return buf;
    }
    return nullptr;
}

char* realpath(const char* path, char* resolved_path) {
    if (!path) return nullptr;
    if (resolved_path) {
        if (path[0] == '/') {
            strncpy(resolved_path, path, 1024);
        } else {
            stbsp_snprintf(resolved_path, 1024, "/app0/%s", path);
        }
        return resolved_path;
    }
    size_t len = strlen(path) + 8;
    char* res = (char*)malloc(len);
    if (!res) return nullptr;
    if (path[0] == '/') {
        strcpy(res, path);
    } else {
        stbsp_snprintf(res, (int)len, "/app0/%s", path);
    }
    return res;
}

int statvfs(const char* path, void* buf) { return -1; }

// POSIX filesystem stubs not present in PS4 libc
ssize_t readlink(const char* path, char* buf, size_t bufsiz) {
    return -1;
}

long syscall(long number, ...) {
    return -1;
}

int symlink(const char* target, const char* linkpath) {
    return -1;
}

int link(const char* oldpath, const char* newpath) {
    return -1;
}

long pathconf(const char* path, int name) {
    return -1;
}

int utimensat(int dirfd, const char* pathname, const struct timespec times[2], int flags) {
    return -1;
}

int fchmodat(int dirfd, const char* pathname, uint32_t mode, int flags) {
    return -1;
}

int openat(int dirfd, const char* pathname, int flags, ...) {
    return -1;
}

int unlinkat(int dirfd, const char* pathname, int flags) {
    return -1;
}

// Locale stubs
locale_t newlocale(int category_mask, const char* locale, locale_t base) {
    return (locale_t)1;
}

locale_t uselocale(locale_t newloc) {
    return (locale_t)1;
}

void freelocale(locale_t locobj) {
}

void* catopen(const char* name, int oflag) {
    return (void*)-1;
}

char* catgets(void* catd, int set_id, int msg_id, const char* s) {
    return const_cast<char*>(s);
}

int catclose(void* catd) {
    return 0;
}

int isalnum_l(int c, locale_t locale) { return isalnum(c); }
int isalpha_l(int c, locale_t locale) { return isalpha(c); }
int isblank_l(int c, locale_t locale) { return isblank(c); }
int iscntrl_l(int c, locale_t locale) { return iscntrl(c); }
int isdigit_l(int c, locale_t locale) { return isdigit(c); }
int isgraph_l(int c, locale_t locale) { return isgraph(c); }
int islower_l(int c, locale_t locale) { return islower(c); }
int isprint_l(int c, locale_t locale) { return isprint(c); }
int ispunct_l(int c, locale_t locale) { return ispunct(c); }
int isspace_l(int c, locale_t locale) { return isspace(c); }
int isupper_l(int c, locale_t locale) { return isupper(c); }
int isxdigit_l(int c, locale_t locale) { return isxdigit(c); }
int toupper_l(int c, locale_t locale) { return toupper(c); }
int tolower_l(int c, locale_t locale) { return tolower(c); }
int iswalnum_l(wint_t wc, locale_t locale) { return iswalnum(wc); }
int iswalpha_l(wint_t wc, locale_t locale) { return iswalpha(wc); }
int iswblank_l(wint_t wc, locale_t locale) { return iswblank(wc); }
int iswcntrl_l(wint_t wc, locale_t locale) { return iswcntrl(wc); }
int iswdigit_l(wint_t wc, locale_t locale) { return iswdigit(wc); }
int iswgraph_l(wint_t wc, locale_t locale) { return iswgraph(wc); }
int iswlower_l(wint_t wc, locale_t locale) { return iswlower(wc); }
int iswprint_l(wint_t wc, locale_t locale) { return iswprint(wc); }
int iswpunct_l(wint_t wc, locale_t locale) { return iswpunct(wc); }
int iswspace_l(wint_t wc, locale_t locale) { return iswspace(wc); }
int iswupper_l(wint_t wc, locale_t locale) { return iswupper(wc); }
int iswxdigit_l(wint_t wc, locale_t locale) { return iswxdigit(wc); }
wint_t towupper_l(wint_t wc, locale_t locale) { return towupper(wc); }
wint_t towlower_l(wint_t wc, locale_t locale) { return towlower(wc); }

size_t strftime_l(char* s, size_t maxsize, const char* format, const struct tm* timeptr, locale_t locale) {
    return strftime(s, maxsize, format, timeptr);
}

int strcoll_l(const char* s1, const char* s2, locale_t locale) {
    return strcmp(s1, s2);
}

size_t strxfrm_l(char* dest, const char* src, size_t n, locale_t locale) {
    return strxfrm(dest, src, n);
}

int wcscoll_l(const wchar_t* ws1, const wchar_t* ws2, locale_t locale) {
    return wcscmp(ws1, ws2);
}

size_t wcsxfrm_l(wchar_t* dest, const wchar_t* src, size_t n, locale_t locale) {
    return wcsxfrm(dest, src, n);
}

size_t wcsnrtombs(char* dst, const wchar_t** src, size_t nwc, size_t len, mbstate_t* ps) {
    return wcsrtombs(dst, src, len, ps);
}

size_t mbsnrtowcs(wchar_t* dst, const char** src, size_t nms, size_t len, mbstate_t* ps) {
    return mbsrtowcs(dst, src, len, ps);
}

size_t __ctype_get_mb_cur_max(void) {
    return 4;
}

float strtof_l(const char* nptr, char** endptr, locale_t locale) {
    return strtof(nptr, endptr);
}

double strtod_l(const char* nptr, char** endptr, locale_t locale) {
    return strtod(nptr, endptr);
}

long double strtold_l(const char* nptr, char** endptr, locale_t locale) {
    return strtold(nptr, endptr);
}

// -------------------------------------------------------------
// POSIX Threads / Mutex / CondVar / Once Implementations
// -------------------------------------------------------------

struct InternalMutex {
    std::atomic<void*> owner;
    uint32_t count;
    int type; // 0 = normal, 1 = recursive, 2 = errorcheck
    std::atomic_flag lock;
};

int pthread_mutexattr_init(pthread_mutexattr_t* attr) {
    if (!attr) return 22; // EINVAL
    memset(attr, 0, sizeof(*attr));
    return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t* attr, int type) {
    if (!attr) return 22;
    *reinterpret_cast<int*>(attr) = type;
    return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t* attr, int* type) {
    if (!attr || !type) return 22;
    *type = *reinterpret_cast<const int*>(attr);
    return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t* attr) {
    return 0;
}

int pthread_mutex_init(pthread_mutex_t* mutex, const pthread_mutexattr_t* attr) {
    if (!mutex) return 22;
    memset(mutex, 0, sizeof(*mutex));
    InternalMutex* m = reinterpret_cast<InternalMutex*>(mutex);
    m->owner.store(nullptr, std::memory_order_relaxed);
    m->count = 0;
    m->type = attr ? *reinterpret_cast<const int*>(attr) : 0;
    m->lock.clear();
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t* mutex) {
    if (!mutex) return 22;
    memset(mutex, 0, sizeof(*mutex));
    return 0;
}

static void* get_current_thread_id() {
    int32_t tid = scePthreadGetthreadid();
    if (tid <= 0) tid = 1;
    return reinterpret_cast<void*>(static_cast<uintptr_t>(tid));
}

int pthread_mutex_lock(pthread_mutex_t* mutex) {
    if (!mutex) return 22;
    InternalMutex* m = reinterpret_cast<InternalMutex*>(mutex);
    void* self = get_current_thread_id();

    if (m->type == 1 /* PTHREAD_MUTEX_RECURSIVE */ && m->owner.load(std::memory_order_relaxed) == self) {
        m->count++;
        return 0;
    }

    while (m->lock.test_and_set(std::memory_order_acquire)) {
        #if defined(__x86_64__)
        __builtin_ia32_pause();
        #endif
    }

    m->owner.store(self, std::memory_order_relaxed);
    m->count = 1;
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t* mutex) {
    if (!mutex) return 22;
    InternalMutex* m = reinterpret_cast<InternalMutex*>(mutex);
    void* self = get_current_thread_id();

    if (m->type == 1 && m->owner.load(std::memory_order_relaxed) == self) {
        m->count++;
        return 0;
    }

    if (!m->lock.test_and_set(std::memory_order_acquire)) {
        m->owner.store(self, std::memory_order_relaxed);
        m->count = 1;
        return 0;
    }
    return 16; // EBUSY
}

int pthread_mutex_unlock(pthread_mutex_t* mutex) {
    if (!mutex) return 22;
    InternalMutex* m = reinterpret_cast<InternalMutex*>(mutex);
    void* self = get_current_thread_id();

    if (m->type == 1 && m->owner.load(std::memory_order_relaxed) == self) {
        m->count--;
        if (m->count > 0) return 0;
    }

    m->owner.store(nullptr, std::memory_order_relaxed);
    m->count = 0;
    m->lock.clear(std::memory_order_release);
    return 0;
}

int pthread_rwlockattr_init(pthread_rwlockattr_t* attr) { return 0; }
int pthread_rwlockattr_destroy(pthread_rwlockattr_t* attr) { return 0; }
int pthread_rwlock_init(pthread_rwlock_t* rw, const pthread_rwlockattr_t* attr) {
    return pthread_mutex_init(reinterpret_cast<pthread_mutex_t*>(rw), nullptr);
}
int pthread_rwlock_destroy(pthread_rwlock_t* rw) {
    return pthread_mutex_destroy(reinterpret_cast<pthread_mutex_t*>(rw));
}
int pthread_rwlock_rdlock(pthread_rwlock_t* rw) {
    return pthread_mutex_lock(reinterpret_cast<pthread_mutex_t*>(rw));
}
int pthread_rwlock_wrlock(pthread_rwlock_t* rw) {
    return pthread_mutex_lock(reinterpret_cast<pthread_mutex_t*>(rw));
}
int pthread_rwlock_tryrdlock(pthread_rwlock_t* rw) {
    return pthread_mutex_trylock(reinterpret_cast<pthread_mutex_t*>(rw));
}
int pthread_rwlock_trywrlock(pthread_rwlock_t* rw) {
    return pthread_mutex_trylock(reinterpret_cast<pthread_mutex_t*>(rw));
}
int pthread_rwlock_unlock(pthread_rwlock_t* rw) {
    return pthread_mutex_unlock(reinterpret_cast<pthread_mutex_t*>(rw));
}

int pthread_condattr_init(pthread_condattr_t* attr) { return 0; }
int pthread_condattr_destroy(pthread_condattr_t* attr) { return 0; }
int pthread_cond_init(pthread_cond_t* cond, const pthread_condattr_t* attr) { return 0; }
int pthread_cond_destroy(pthread_cond_t* cond) { return 0; }
int pthread_cond_broadcast(pthread_cond_t* cond) { return 0; }
int pthread_cond_signal(pthread_cond_t* cond) { return 0; }
int pthread_cond_wait(pthread_cond_t* cond, pthread_mutex_t* mutex) {
    if (mutex) pthread_mutex_unlock(mutex);
    #if defined(__x86_64__)
    __builtin_ia32_pause();
    #endif
    if (mutex) pthread_mutex_lock(mutex);
    return 0;
}
int pthread_cond_timedwait(pthread_cond_t* cond, pthread_mutex_t* mutex, const struct timespec* abstime) {
    return pthread_cond_wait(cond, mutex);
}

int pthread_once(pthread_once_t* control, void (*init_routine)(void)) {
    if (!control || !init_routine) return 22;
    static std::atomic_flag s_once_lock = ATOMIC_FLAG_INIT;
    int* val = reinterpret_cast<int*>(control);
    if (*val == 2) return 0;

    while (s_once_lock.test_and_set(std::memory_order_acquire)) {
        #if defined(__x86_64__)
        __builtin_ia32_pause();
        #endif
    }

    if (*val != 2) {
        init_routine();
        *val = 2;
    }

    s_once_lock.clear(std::memory_order_release);
    return 0;
}

// -------------------------------------------------------------
// Sleep and Timer Stubs
// -------------------------------------------------------------
int usleep(useconds_t usec) {
    return sceKernelUsleep(usec);
}

unsigned int sleep(unsigned int seconds) {
    sceKernelSleep(seconds);
    return 0;
}

int nanosleep(const struct timespec* req, struct timespec* rem) {
    if (!req) return -1;
    OrbisKernelTimespec oreq = { (int64_t)req->tv_sec, (int64_t)req->tv_nsec };
    return sceKernelNanosleep(&oreq, nullptr);
}

int select(int nfds, fd_set* readfds, fd_set* writefds, fd_set* exceptfds, struct timeval* timeout) {
    if (nfds == 0 || (!readfds && !writefds && !exceptfds)) {
        if (timeout) {
            uint64_t usec = (uint64_t)timeout->tv_sec * 1000000ULL + timeout->tv_usec;
            sceKernelUsleep((uint32_t)usec);
        }
        return 0;
    }
    return 0;
}

// -------------------------------------------------------------
// Math Stubs (bypassing missing libc NIDs in shadPS4)
// -------------------------------------------------------------
float atan2f(float y, float x) {
    return __builtin_atan2f(y, x);
}

double atan2(double y, double x) {
    return __builtin_atan2(y, x);
}

// Dynamic linking introspection
int dladdr(const void* addr, void* info) {
    return 0;
}

int dl_iterate_phdr(int (*callback)(void* info, size_t size, void* data), void* data) {
    return 0;
}

// C++ ABI stubs
int __cxa_atexit(void (*func)(void*), void* arg, void* dso_handle) {
    return 0;
}

void __cxa_finalize(void* dso_handle) {
}

extern void (*__init_array_start[])(void) __attribute__((weak));
extern void (*__init_array_end[])(void) __attribute__((weak));

static void call_init_array() {
    if (__init_array_start && __init_array_end) {
        ptrdiff_t count = __init_array_end - __init_array_start;
        for (ptrdiff_t i = 0; i < count; ++i) {
            if (__init_array_start[i]) {
                __init_array_start[i]();
            }
        }
    }
}

// Entry point bridge: musl's _start calls __libc_start_main(main, argc, argv, init, fini, ldso_fini)
// We implement this locally so the binary never needs to resolve it from an external PS4 lib.
extern "C" int main(int argc, char** argv);

int __libc_start_main(
    int (*main_func)(int, char**, char**),
    int argc,
    char** argv,
    void (*init_func)(void),
    void (*fini_func)(void),
    void (*ldso_fini)(void))
{
    ps4_klog("[MyEngine PS4] __libc_start_main entered\n");
    if (init_func) init_func();
    call_init_array();
    ps4_klog("[MyEngine PS4] .init_array execution completed\n");
    int result = main_func(argc, argv, nullptr);
    if (fini_func) fini_func();
    return result;
}

FILE* popen(const char* /*command*/, const char* /*mode*/) {
    return nullptr;
}

int pclose(FILE* /*stream*/) {
    return -1;
}

pid_t fork(void) {
    return -1;
}

int execvp(const char* /*file*/, char* const /*argv*/[]) {
    return -1;
}

pid_t waitpid(pid_t /*pid*/, int* /*status*/, int /*options*/) {
    return -1;
}

void qsort(void* base, size_t nel, size_t width, int (*compar)(const void*, const void*)) {
    if (!base || nel <= 1 || width == 0 || !compar) return;
    char* a = static_cast<char*>(base);
    for (size_t i = 1; i < nel; ++i) {
        for (size_t j = i; j > 0 && compar(a + (j - 1) * width, a + j * width) > 0; --j) {
            char* p1 = a + (j - 1) * width;
            char* p2 = a + j * width;
            for (size_t k = 0; k < width; ++k) {
                char tmp = p1[k];
                p1[k] = p2[k];
                p2[k] = tmp;
            }
        }
    }
}

float acosf(float x) {
    if (x <= -1.0f) return 3.14159265358979323846f;
    if (x >= 1.0f) return 0.0f;
    return std::acos(x);
}

time_t time(time_t* tloc) {
    OrbisKernelTimeval tv = {};
    sceKernelGettimeofday(&tv);
    time_t t = static_cast<time_t>(tv.tv_sec);
    if (tloc) *tloc = t;
    return t;
}

// -------------------------------------------------------------
// Character & String Conversion Functions (Overriding shadPS4 stubs)
// -------------------------------------------------------------
int tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c + ('a' - 'A');
    return c;
}

int toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - ('a' - 'A');
    return c;
}

unsigned long long strtoull(const char* nptr, char** endptr, int base) {
    const char* s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') s++;

    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    else if (*s == '+') { s++; }

    if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
        base = 16;
    } else if (base == 0) {
        base = (s[0] == '0') ? 8 : 10;
    }

    unsigned long long acc = 0;
    bool any = false;
    while (*s) {
        int d = -1;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        if (d < 0 || d >= base) break;
        acc = acc * base + d;
        any = true;
        s++;
    }

    if (endptr) *endptr = const_cast<char*>(any ? s : nptr);
    return neg ? (0ULL - acc) : acc;
}

long long strtoll(const char* nptr, char** endptr, int base) {
    return static_cast<long long>(strtoull(nptr, endptr, base));
}

long strtol(const char* nptr, char** endptr, int base) {
    return static_cast<long>(strtoull(nptr, endptr, base));
}

unsigned long strtoul(const char* nptr, char** endptr, int base) {
    return static_cast<unsigned long>(strtoull(nptr, endptr, base));
}

int atoi(const char* nptr) {
    return static_cast<int>(strtol(nptr, nullptr, 10));
}

long atol(const char* nptr) {
    return strtol(nptr, nullptr, 10);
}

long long atoll(const char* nptr) {
    return strtoll(nptr, nullptr, 10);
}

double strtod(const char* nptr, char** endptr) {
    const char* s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') s++;

    bool neg = false;
    if (*s == '-') { neg = true; s++; }
    else if (*s == '+') { s++; }

    // Check for special values (nan, inf)
    if ((s[0] == 'n' || s[0] == 'N') && (s[1] == 'a' || s[1] == 'A') && (s[2] == 'n' || s[2] == 'N')) {
        if (endptr) *endptr = const_cast<char*>(s + 3);
        return 0.0 / 0.0;
    }
    if ((s[0] == 'i' || s[0] == 'I') && (s[1] == 'n' || s[1] == 'N') && (s[2] == 'f' || s[2] == 'F')) {
        if (endptr) *endptr = const_cast<char*>(s + 3);
        return neg ? (-1.0 / 0.0) : (1.0 / 0.0);
    }

    double integerPart = 0.0;
    bool anyDigits = false;
    while (*s >= '0' && *s <= '9') {
        integerPart = integerPart * 10.0 + (*s - '0');
        anyDigits = true;
        s++;
    }

    double fractionPart = 0.0;
    double fractionDiv = 1.0;
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            fractionPart = fractionPart * 10.0 + (*s - '0');
            fractionDiv *= 10.0;
            anyDigits = true;
            s++;
        }
    }

    if (!anyDigits) {
        if (endptr) *endptr = const_cast<char*>(nptr);
        return 0.0;
    }

    double result = integerPart + (fractionPart / fractionDiv);

    // Optional scientific notation exponent
    if (*s == 'e' || *s == 'E') {
        const char* expStart = s;
        s++;
        bool expNeg = false;
        if (*s == '-') { expNeg = true; s++; }
        else if (*s == '+') { s++; }

        int expVal = 0;
        bool anyExpDigits = false;
        while (*s >= '0' && *s <= '9') {
            expVal = expVal * 10 + (*s - '0');
            anyExpDigits = true;
            s++;
        }

        if (anyExpDigits) {
            double p = 1.0;
            double b = 10.0;
            int e = expVal;
            while (e > 0) {
                if (e & 1) p *= b;
                b *= b;
                e >>= 1;
            }
            if (expNeg) result /= p;
            else result *= p;
        } else {
            s = expStart; // rollback
        }
    }

    if (endptr) *endptr = const_cast<char*>(s);
    return neg ? -result : result;
}

float strtof(const char* nptr, char** endptr) {
    return static_cast<float>(strtod(nptr, endptr));
}

long double strtold(const char* nptr, char** endptr) {
    return static_cast<long double>(strtod(nptr, endptr));
}

double atof(const char* nptr) {
    return strtod(nptr, nullptr);
}

} // extern "C"

#endif
