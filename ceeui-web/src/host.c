/* SPDX-License-Identifier: MIT
   The web host's environment: what a freestanding CeeSafe module is given by whoever links it. Memory comes from one
   static block, handed out in order and never taken back; the four C functions a compiler may call for copies are
   here; a panic is the page's (an import), and does not return. */
#include <stddef.h>
#include <stdint.h>

#include "ceesafe/rt.h"

#define WEB_MEMORY ((size_t)384 << 20)
static unsigned char web_memory[WEB_MEMORY];
static size_t web_used;

__attribute__((export_name("web_take"))) unsigned char *web_take(size_t size) {
    size = (size + 15u) & ~(size_t)15u;
    if (size > WEB_MEMORY - web_used) {
        return NULL;
    }
    unsigned char *p = web_memory + web_used;
    web_used += size;
    return p;
}

void *memcpy(void *to, const void *from, size_t n) {
    unsigned char *t = (unsigned char *)to;
    const unsigned char *f = (const unsigned char *)from;
    for (size_t k = 0; k < n; k++) {
        t[k] = f[k];
    }
    return to;
}

void *memmove(void *to, const void *from, size_t n) {
    unsigned char *t = (unsigned char *)to;
    const unsigned char *f = (const unsigned char *)from;
    if (t < f) {
        for (size_t k = 0; k < n; k++) {
            t[k] = f[k];
        }
    } else {
        for (size_t k = n; k-- > 0;) {
            t[k] = f[k];
        }
    }
    return to;
}

void *memset(void *to, int value, size_t n) {
    unsigned char *t = (unsigned char *)to;
    for (size_t k = 0; k < n; k++) {
        t[k] = (unsigned char)value;
    }
    return to;
}

int memcmp(const void *a, const void *b, size_t n) {
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    for (size_t k = 0; k < n; k++) {
        if (x[k] != y[k]) {
            return x[k] < y[k] ? -1 : 1;
        }
    }
    return 0;
}

/* A 128-bit product, which the compiler's own library has elsewhere: checked 64-bit multiplication asks for it. */
__int128 __multi3(__int128 a, __int128 b) {
    uint64_t al = (uint64_t)a, ah = (uint64_t)((unsigned __int128)a >> 64);
    uint64_t bl = (uint64_t)b, bh = (uint64_t)((unsigned __int128)b >> 64);
    uint64_t a0 = al & 0xffffffffu, a1 = al >> 32, b0 = bl & 0xffffffffu, b1 = bl >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t middle = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
    uint64_t low = (p00 & 0xffffffffu) | (middle << 32);
    uint64_t high = p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32) + al * bh + ah * bl;
    return (__int128)(((unsigned __int128)high << 64) | low);
}

/* The page's: it throws, so it does not return. */
__attribute__((import_module("env"), import_name("web_panic"), noreturn)) void web_panic(int kind, const char *file,
                                                                                         uint32_t line, uint32_t column);

_Noreturn void ceesafe_panic(cee_panic_kind kind, cee_loc loc) {
    web_panic((int)kind, loc.file, loc.line, loc.column);
}
