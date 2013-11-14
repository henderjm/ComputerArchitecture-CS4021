#pragma once

//
// helper.h
//
// Copyright © 2011 - 2013 jones@scss.tcd.ie
//
// This program is free software; you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free Software Foundation;
// either version 2 of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
// without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//

#include "stdafx.h"         // pre-compiled headers

#ifdef WIN32
#include <intrin.h>         // intrinsics
#else
#include <unistd.h>         // usleep
#include <cpuid.h>          // cpuid
#include <string.h>         // strcpy
#include <pthread.h>        // pthread_create
#include <immintrin.h>      // RTM intrinsics also needs gcc flag -mrtm
#endif

#ifdef WIN32

#define CPUID(cd, v) __cpuid((int*) &cd, v);
#define CPUIDEX(cd, v0, v1) __cpuidex((int*) &cd, v0, v1)

#define THREADH HANDLE

#define WORKERF DWORD (WINAPI *worker) (void*)
#define WORKER DWORD WINAPI

#define TLSINDEX DWORD
#define TLSALLOC(key) key = TlsAlloc()
#define TLSSETVALUE(tlsIndex, v) TlsSetValue(tlsIndex, v)
#define TLSGETVALUE(tlsIndex) (int) TlsGetValue(tlsIndex)

#else // linux

#define UINT    unsigned int
#define UINT64  unsigned long long
#define LONG64  unsigned long long
#define PVOID   void*
#define MAXINT  INT_MAX

#define CPUID(cd, v) __cpuid(v, cd.eax, cd.ebx, cd.ecx, cd.edx);
#define CPUIDEX(cd, v0, v1) __cpuid_count(v0, v1, cd.eax, cd.ebx, cd.ecx, cd.edx)

#define THREADH pthread_t
#define GetCurrentProcessorNumber() sched_getcpu()

#define WORKER void*
#define WORKERF void* (*worker) (void*)

#define _aligned_malloc(sz, align)  aligned_alloc(align, sz)
#define _aligned_free(p) free(p)
#define _alloca alloca

#define strcpy_s(dst, sz, src) strcpy(dst, src)

#define InterlockedIncrement(addr)                                  __sync_fetch_and_add(addr, 1)
#define InterlockedIncrement64(addr)                                __sync_fetch_and_add(addr, 1)
#define InterlockedExchange(addr, v)                                __sync_lock_test_and_set(addr, v)
#define InterlockedExchangePointer(addr, v)                         __sync_lock_test_and_set(addr, v)
#define InterlockedExchangeAdd(addr, v)                             __sync_fetch_and_add(addr, v)
#define InterlockedCompareExchange(addr, newv, oldv)                __sync_val_compare_and_swap(addr, oldv, newv)
#define InterlockedCompareExchange64(addr, newv, oldv)              __sync_val_compare_and_swap(addr, oldv, newv)
#define InterlockedCompareExchangePointer(addr, newv, oldv)         __sync_val_compare_and_swap(addr, oldv, newv)
#define _InterlockedCompareExchange_HLEAcquire(addr, newv, oldv)    __atomic_exchange_n(addr, newv, __ATOMIC_ACQUIRE | __ATOMIC_HLE_ACQUIRE)
#define _Store_HLERelease(addr, v)                                  __atomic_store_n(addr, v, __ATOMIC_RELEASE | __ATOMIC_HLE_RELEASE)

#define _mm_pause() __builtin_ia32_pause()
#define _mm_mfence() __builtin_ia32_mfence()

#define TLSINDEX pthread_key_t
#define TLSALLOC(key) pthread_key_create(&key, NULL)
#define TLSSETVALUE(key, v) pthread_setspecific(key, v)
#define TLSGETVALUE(key) (size_t) pthread_getspecific(key)

#define Sleep(ms) usleep((ms)*1000)

#endif

extern int ncpus;                                                   // # logical CPUs

extern void getDateAndTime(char*, int);                             // getDateAndTime
extern char* getHostName();                                         // get host name
extern char* getOSName();                                           // get OS name
extern int getNumberOfCPUs();                                       // get number of CPUs
extern UINT64 getPhysicalMemSz();                                   // get RAM sz in bytes
extern int is64bitExe();                                            // return 1 if 64 bit .exe

extern UINT64 getWallClockMS();                                     // get wall clock in milliseconds from some epoch
extern void createThread(THREADH*, WORKERF, void*);                 //
extern void runThreadOnCPU(int);                                    // run thread on cpu
extern void waitForThreadsToFinish(int, THREADH*);                  //
extern void closeThread(THREADH);                                   //

extern int cpu64bit();                                              // return 1 if cpu is 64 bit
extern int cpuFamily();                                             // cpu family
extern int cpuModel();                                              // cpu model
extern int cpuStepping();                                           // cpu stepping
extern char *cpuBrandString();                                      // cpu brand string

extern int rtmSupported();                                          // return 1 if RTM supported (restricted transactional memory)
extern int hleSupported();                                          // return 1 if HLE supported (hardware lock Elision)
extern int getCacheLineSz();                                        // get cache line sz

extern void pauseIfKeyPressed();                                    // pause if key pressed
extern void quit(int = 0);                                          // quit

//
// performance monitoring
//

#define FIXED_CTR_RING0                     (1ULL)
#define FIXED_CTR_RING123                   (2ULL)
#define FIXED_CTR_RING0123                  (0x03ULL)

#define PERFEVTSEL_USR                      (1ULL << 16)
#define PERFEVTSEL_OS                       (1ULL << 17)
#define PERFEVTSEL_EN                       (1ULL << 22)
#define PERFEVTSEL_IN_TX                    (1ULL << 32)
#define PERFEVTSEL_IN_TXCP                  (1ULL << 33)

#define CPU_CLK_UNHALTED_THREAD_P           ((0x00 << 8) | 0x3c)        // mask | event
#define CPU_CLK_UNHALTED_THREAD_REF_XCLK    ((0x01 << 8) | 0x3c)        // mask | event
#define INST_RETIRED_ANY_P                  ((0x00 << 8) | 0xc0)        // mask | event
#define RTM_RETIRED_START                   ((0x01 << 8) | 0xc9)        // mask | event
#define RTM_RETIRED_COMMIT                  ((0x02 << 8) | 0xc9)        // mask | event

extern int openPMS();                       // open PMS
extern void closePMS();                     // close PMS
extern int pmversion();                     // return performance monitoring version
extern int nfixedCtr();                     // return # of fixed performance counters
extern int fixedCtrW();                     // return width of fixed counters
extern int npmc();                          // return # performance counters
extern int pmcW();                          // return width of performance counters

extern UINT64 readMSR(int, int);
extern void writeMSR(int, int, UINT64);

extern UINT64 readFIXED_CTR(int, int);
extern void writeFIXED_CTR(int, int, UINT64);

extern UINT64 readFIXED_CTR_CTRL(int);
extern void writeFIXED_CTR_CTRL(int, UINT64);

extern UINT64 readPERF_GLOBAL_STATUS(int);
extern void writePERF_GLOBAL_STATUS(int, UINT64);

extern UINT64 readPERF_GLOBAL_CTRL(int);
extern void writePERF_GLOBAL_CTRL(int, UINT64);

extern UINT64 readPERF_GLOBAL_OVF_CTRL(int);
extern void writePERF_GLOBAL_OVR_CTRL(int, UINT64);

extern UINT64 readPERFEVTSEL(int, int);
extern void writePERFEVTSEL(int, int, UINT64);

extern UINT64 readPMC(int, int);
extern void writePMC(int, int, UINT64);

// eof