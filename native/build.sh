#!/bin/sh
# Builds libtoolscreen.so (x86_64). Output: native/build/libtoolscreen.so
set -e
cd "$(dirname "$0")"
mkdir -p build
rm -f build/*.o
CXXFLAGS="${OPT:--O2} -std=c++17 -fPIC -fvisibility=hidden -include src/glibc_compat.h -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-nonnull-compare"
for f in hooks config input render gamestate expression_parser ninjabrain; do
    g++ $CXXFLAGS -c src/$f.cpp -o build/$f.o &
done
wait
gcc -O2 -std=c11 -fPIC -c src/glibc_compat.c -o build/glibc_compat.o
g++ -c -I assets src/embed.S -o build/embed.o
# libstdc++ is linked statically (the JVM may carry its own); libgcc_s stays shared, which every system has.
g++ -shared -o build/libtoolscreen.so build/*.o -static-libstdc++ -ldl -lpthread \
    -Wl,--version-script=src/exports.map -Wl,--exclude-libs,ALL -Wl,--no-undefined -Wl,-z,defs
[ -n "$NOSTRIP" ] || strip --strip-unneeded build/libtoolscreen.so
MAXV=$(objdump -T build/libtoolscreen.so | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
echo "built $(pwd)/build/libtoolscreen.so (requires $MAXV)"
