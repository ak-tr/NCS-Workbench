// XP-safe standard library use with llvm-mingw's static libc++.
//
// libc++ ships std::string/std::wstring precompiled with exceptions; linking
// that code drags in libc++abi + libunwind, which import Vista-only kernel32
// functions (SRW locks, FlsAlloc, InitOnce). A string with a custom allocator
// isn't precompiled, so it is built from headers under -fno-exceptions.
//
// Rules: use String/WString below, never std::string/std::wstring.
// Header-only containers and algorithms (vector, map, sort, ...) are fine.
// No Windows headers here, so trace.h can use it too.

#pragma once

#include <cstddef>
#include <cstdlib>
#include <string>

template <class T>
struct XpAlloc {
    typedef T value_type;

    XpAlloc() {}
    template <class U>
    XpAlloc(const XpAlloc<U>&) {}

    T* allocate(std::size_t n)
    {
        void* p = std::malloc(n * sizeof(T));
        if (!p)
            std::abort();
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t) { std::free(p); }
};

template <class T, class U>
bool operator==(const XpAlloc<T>&, const XpAlloc<U>&) { return true; }
template <class T, class U>
bool operator!=(const XpAlloc<T>&, const XpAlloc<U>&) { return false; }

typedef std::basic_string<char, std::char_traits<char>, XpAlloc<char> > String;
typedef std::basic_string<wchar_t, std::char_traits<wchar_t>, XpAlloc<wchar_t> > WString;
