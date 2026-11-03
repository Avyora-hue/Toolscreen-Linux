/* Force-included into every C++ unit: bind libm symbols to versions every x86_64 glibc has, so the library
   loads on older distributions and Flatpak runtimes (baseline glibc 2.34). */
#pragma once
__asm__(".symver sqrtf,sqrtf@GLIBC_2.2.5");
__asm__(".symver acosf,acosf@GLIBC_2.2.5");
__asm__(".symver fmodf,fmodf@GLIBC_2.2.5");
__asm__(".symver logf,logf@GLIBC_2.2.5");
__asm__(".symver expf,expf@GLIBC_2.2.5");
__asm__(".symver powf,powf@GLIBC_2.2.5");
__asm__(".symver exp2f,exp2f@GLIBC_2.2.5");
__asm__(".symver log2f,log2f@GLIBC_2.2.5");
