// Replacements for the libc++ pieces that would otherwise pull in libc++abi
// and libunwind (see xpstd.h). Out of memory or a container misuse aborts.

#include <__verbose_abort>
#include <cstdlib>
#include <new>

void* operator new(std::size_t n)
{
    void* p = std::malloc(n ? n : 1);
    if (!p)
        std::abort();
    return p;
}

void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

void std::__libcpp_verbose_abort(char const*, ...) noexcept { std::abort(); }
