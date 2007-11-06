
/* Copyright (c) 2007, Stefan Eilemann <eile@equalizergraphics.com> 
   All rights reserved. */

#ifndef EQBASE_TYPES_H
#define EQBASE_TYPES_H

#include <sys/types.h>

#ifdef sgi
typedef int socklen_t;
#endif

#ifdef Darwin
#  include <crt_externs.h>
#  define environ (*_NSGetEnviron())
#else
extern "C" char **environ;
#endif

#ifdef WIN32
typedef int        socklen_t;

#  ifdef WIN32_VC
typedef UINT64     uint64_t;
typedef INT64      int64_t;
typedef UINT32     uint32_t;
typedef INT32      int32_t;
typedef UINT16     uint16_t;
typedef UINT8      uint8_t;
#    ifndef HAVE_SSIZE_T
typedef SSIZE_T    ssize_t;
#    endif
#  endif // Win32, Visual C++
#endif // Win32

#define EQ_BIT1  (0x00000001u)
#define EQ_BIT2  (0x00000002u)
#define EQ_BIT3  (0x00000004u)
#define EQ_BIT4  (0x00000008u)
#define EQ_BIT5  (0x00000010u)
#define EQ_BIT6  (0x00000020u)
#define EQ_BIT7  (0x00000040u)
#define EQ_BIT8  (0x00000080u)

#define EQ_BIT9  (0x00000100u)
#define EQ_BIT10 (0x00000200u)
#define EQ_BIT11 (0x00000400u)
#define EQ_BIT12 (0x00000800u)
#define EQ_BIT13 (0x00001000u)
#define EQ_BIT14 (0x00002000u)
#define EQ_BIT15 (0x00004000u)
#define EQ_BIT16 (0x00008000u)

#define EQ_BIT17 (0x00100000u)
#define EQ_BIT18 (0x00200000u)
#define EQ_BIT19 (0x00400000u)
#define EQ_BIT20 (0x00800000u)
#define EQ_BIT21 (0x00100000u)
#define EQ_BIT22 (0x00200000u)
#define EQ_BIT23 (0x00400000u)
#define EQ_BIT24 (0x00800000u)

#define EQ_BIT25 (0x01000000u)
#define EQ_BIT26 (0x02000000u)
#define EQ_BIT27 (0x04000000u)
#define EQ_BIT28 (0x08000000u)
#define EQ_BIT29 (0x10000000u)
#define EQ_BIT30 (0x20000000u)
#define EQ_BIT31 (0x40000000u)
#define EQ_BIT32 (0x80000000u)

#define EQ_BIT_ALL  (0xffffffffu)
#define EQ_BIT_NONE (0)

#endif //EQBASE_TYPES_H
