/* Local definitions of symbols that newer glibc headers or the static C++ runtime would otherwise import
   from glibc >= 2.36. Compiled as plain C11 so strtol here is the classic function. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/random.h>

__attribute__((visibility("hidden"))) long __isoc23_strtol(const char* s, char** end, int base) { return strtol(s, end, base); }
__attribute__((visibility("hidden"))) unsigned long __isoc23_strtoul(const char* s, char** end, int base) { return strtoul(s, end, base); }
__attribute__((visibility("hidden"))) long long __isoc23_strtoll(const char* s, char** end, int base) { return strtoll(s, end, base); }
__attribute__((visibility("hidden"))) unsigned long long __isoc23_strtoull(const char* s, char** end, int base) { return strtoull(s, end, base); }

__attribute__((visibility("hidden"))) uint32_t arc4random(void) {
    uint32_t v = 0;
    while (getrandom(&v, sizeof(v), 0) != (long)sizeof(v)) {
    }
    return v;
}

__attribute__((visibility("hidden"))) int __isoc23_sscanf(const char* s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vsscanf(s, fmt, ap);
    va_end(ap);
    return r;
}
