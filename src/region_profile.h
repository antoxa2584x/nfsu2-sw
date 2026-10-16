#ifndef NFSU2_REGION_PROFILE_H
#define NFSU2_REGION_PROFILE_H
/* An app contains statically translated code for exactly one executable. */
#define NFSU2_XBE_SIZE 4038656ull
#ifdef NFSU2_REGION_PAL
#define NFSU2_REGION_NAME "PAL Europe (En/Fr/De/It/Nl/Sv)"
#define NFSU2_XBE_SHA1 "144deb954e0e9208e0b7e08e09b4e5e67b2f3356"
#define NFSU2_ENTRY_POINT 0x0021B77Eu
#else
#define NFSU2_REGION_NAME "NTSC-U"
#define NFSU2_XBE_SHA1 "428b5a70b9400f19cc5be7679db6c72a6f80a1c9"
#define NFSU2_ENTRY_POINT 0x0021B1CEu
#endif
#endif
