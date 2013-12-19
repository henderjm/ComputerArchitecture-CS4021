//
// helper.cpp
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

//
// 12/01/11 first version
// 15/07/13 added performance monitoring support
// 14/09/13 linux support (needs g++ 4.8 or later)
//

//
// NB: gcc: need to compile with flag -mrtm
//

#include "stdafx.h"         // pre-compiled headers
#include <iostream>         // cout
#include <iomanip>          // setprecision
#include "helper.h"         //

#ifdef WIN32
#include <conio.h>          // _getch()
#else
#include <unistd.h>         //
#include <limits.h>         // HOST_NAME_MAX
#include <sys/utsname.h>    //
#include <fcntl.h>          // O_RDWR
#endif

using namespace std;        // cout. ...

//
// for data returned by cpuid instruction
//
struct _cd {
    UINT eax;
    UINT ebx;
    UINT ecx;
    UINT edx;
} cd;

int ncpus;                  // # logical CPUs
char *hostName = NULL;      // host name
char *osName = NULL;        // os name
char *brandString = NULL;   // cpu brand string
int counter = 0;
//
// getDateAndTime
//
void getDateAndTime(char *dateAndTime, int sz)
{
    time_t t = time(NULL);
#ifdef WIN32
    struct tm now;
    localtime_s(&now, &t);
    strftime(dateAndTime, sz, "%d-%b-%Y %H:%M:%S", &now);
#else
    struct tm *now = localtime(&t);
    strftime(dateAndTime, sz, "%d-%b-%Y %H:%M:%S", now);
#endif
}

//
// getHostName
//
char* getHostName()
{
    if (hostName == NULL) {

#ifdef WIN32
        DWORD sz = (MAX_COMPUTERNAME_LENGTH + 1) * sizeof(char);
        hostName = (char*) malloc(sz);
        GetComputerNameA(hostName, &sz);
#else
        size_t sz = (HOST_NAME_MAX + 1) * sizeof(char);
        hostName = (char*) malloc(sz);
        gethostname(hostName, sz);
#endif

    }
    return hostName;
}

//
// getOSName
//
char* getOSName()
{
    if (osName == NULL) {

        osName = (char*) malloc(256);   // should be large enough

#ifdef WIN32
        DWORD sz = 256;
        RegGetValueA(HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows NT\\CurrentVersion", "ProductName", RRF_RT_ANY, NULL, (LPBYTE) osName, &sz);
#ifdef _WIN64
        strcat_s(osName, 256, " (64 bit)");
#else
        int win64;
        IsWow64Process(GetCurrentProcess(), &win64);
        strcat_s(osName, 256, win64 ? " (64 bit)" : " (32 bit)");
#endif
#else
        struct utsname utsName;
        uname(&utsName);
        strcpy(osName, utsName.sysname);
        strcat(osName,  " ");
        strcat(osName, utsName.release);
#endif

    }
    return osName;
}

//
// is64bitExe
//
// return 1 if a 64 bit .exe
// return 0 if a 32 bit .exe
//
int is64bitExe()
{
    return sizeof(size_t) == 8;
}

//
// getPhysicalMemSz
//
UINT64 getPhysicalMemSz()
{
#ifdef WIN32
    UINT64 v;
    GetPhysicallyInstalledSystemMemory(&v);                         // returns KB
    return v * 1024;                                                // now bytes
#else
    return (UINT64) sysconf(_SC_PHYS_PAGES)* sysconf(_SC_PAGESIZE); // NB: returns bytes
#endif
}

//
// getNumberOfCPUs
//
int getNumberOfCPUs()
{
#ifdef WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo );
    return sysinfo.dwNumberOfProcessors;
#else
    return sysconf(_SC_NPROCESSORS_ONLN);
#endif
}

//
// cpu64bit
//
int cpu64bit()
{
    CPUID(cd, 0x80000001);
    return (cd.edx >> 29) & 0x01;
}

//
// cpuFamily
//
int cpuFamily()
{
    CPUID(cd, 0x01);
    return (cd.eax >> 8) & 0xff;
}

//
// cpuModel
//
int cpuModel()
{
    CPUID(cd, 0x01);
    if (((cd.eax >> 8) & 0xff) == 0x06)
        return (cd.eax >> 12 & 0xf0) + ((cd.eax >> 4) & 0x0f);
    return (cd.eax >> 4) & 0x0f;
}

//
// cpuStepping
//
int cpuStepping()
{
    CPUID(cd, 0x01);
    return cd.eax & 0x0f;
}

//
// cpuBrandString
//
char *cpuBrandString()
{
    if (brandString)
        return brandString;

    brandString = (char*) calloc(16*3, sizeof(char));
    
    CPUID(cd, 0x80000000);

    if (cd.eax < 0x80000004) {
        strcpy_s(brandString, 16*3, "unknown");
        return brandString;
    }

    for (int i = 0; i < 3; i++) {
        CPUID(cd, 0x80000002 + i);
        UINT *p = &cd.eax;
        for (int j = 0; j < 4; j++, p++) {
            for (int k = 0; k < 4; k++ ) {
                brandString[i*16 + j*4 + k] = (*p >> (k * 8)) & 0xff;
            }
        }
    }
    return brandString;
}

//
// rtmSupported (restricted transactional memory)
//
// NB: VirtualBox returns 0 even if supported??
//
int rtmSupported()
{
    CPUIDEX(cd, 0x07, 0);
    return (cd.ebx >> 11) & 1;      // test bit 11 in returned ebx
}

//
// hleSupported (hardware lock ellision)
//
// NB: VirtualBox returns 0 even if supported??
//
int hleSupported()
{
    CPUIDEX(cd, 0x07, 0);
    return (cd.ebx >> 4) & 1;       // test bit 4 in returned ebx
}

//
// look for L1 cache line size (see Intel Application note on CPUID instruction)
//
int lookForL1DataCacheInfo(int v)
{
    if (v & 0x80000000)
        return 0;

    for (int i = 0; i < 4; i++) {
        switch (v & 0xff) {
        case 0x0a:
        case 0x0c:
        case 0x10:
            return 32;
        case 0x0e:
        case 0x2c:
        case 0x60:
        case 0x66:
        case 0x67:
        case 0x68:
            return 64;
        }
        v >>= 8;
    }
    return 0;
}

//
// getL1DataCacheInfo
//
int getL1DataCacheInfo()
{
    CPUID(cd, 2);

    if ((cd.eax & 0xff) != 1) {
        cout << "unrecognised cache type: default L 64" << endl;
        return 64;
    }

    int sz;

    if ((sz = lookForL1DataCacheInfo(cd.eax & ~0xff)))
        return sz;
    if ((sz = lookForL1DataCacheInfo(cd.ebx)))
        return sz;
    if ((sz = lookForL1DataCacheInfo(cd.ecx)))
        return sz;
    if ((sz = lookForL1DataCacheInfo(cd.edx)))
        return sz;

    cout << "unrecognised cache type: default L 64" << endl;
    return 64;
}

//
// getDeterministicCacheInfo
//
int getDeterministicCacheInfo()
{
    int type, ways, partitions, lineSz = 0, sets;
    int i = 0;
    while (1) {
        CPUIDEX(cd, 0x04, i);
        type = cd.eax & 0x1f;
        if (type == 0)
            break;
        cout << "L" << ((cd.eax >> 5) & 0x07);
        cout << ((type == 1) ? " D" : (type == 2) ? " I" : " U");
        ways = ((cd.ebx >> 22) & 0x03ff) + 1;
        partitions = ((cd.ebx) >> 12 & 0x03ff) + 1;
        sets = cd.ecx + 1;
        lineSz = (cd.ebx & 0x0fff) + 1;
        cout << " " << setw(5) << ways*partitions*lineSz*sets/1024 << "K" << " L" << setw(3) << lineSz << " K" << setw(3) << ways << " N" << setw(5) << sets;
        cout << endl;
        i++;
    }
    return lineSz;
}

//
// getCacheLineSz
//
int getCacheLineSz()
{
    CPUID(cd, 0x00);
    if (cd.eax >= 4)
        return getDeterministicCacheInfo();
    return getL1DataCacheInfo();
}

//
// getWallClockMS
//
UINT64 getWallClockMS()
{
#ifdef WIN32
    return clock() * 1000 / CLOCKS_PER_SEC;
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec*1000 + t.tv_nsec / 1000000;
#endif
}

//
// setThreadCPU
//
void createThread(THREADH *threadH, WORKERF, void *arg)
{
#ifdef WIN32
    *threadH = CreateThread(NULL, 0, worker, arg, 0, NULL);
#else
    pthread_create(threadH, NULL, worker, arg);
#endif
}

//
// runThreadOnCPU
//
void runThreadOnCPU(int cpu)
{
#ifdef WIN32
    SetThreadAffinityMask(GetCurrentThread(), 1 << cpu);
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
}

//
// closeThread
//
void closeThread(THREADH threadH)
{
#ifdef WIN32
//	cout << threadH << " : counter = " << counter << endl;
    CloseHandle(threadH);
#else
    // nothing to do
#endif
}

//
// waitForThreadsToFinish
//
void waitForThreadsToFinish(int nt, THREADH *threadH)
{
#ifdef WIN32
    WaitForMultipleObjects(nt, threadH, true, INFINITE);
#else
    for (int thread = 0; thread < nt; thread++)
        pthread_join(threadH[thread], NULL);
#endif
}

//
// Processor monitoring support (PMS)
//
// NB: see Intel Performance Counter Monitor v2.5.1
// NB: simplified for HLE and RTM performance measurement
//
// NB: FIXED_CTR0 counts instructions retired (see Vol 3C 35-17)
// NB: FIXED_CTR1 counts unhalted core cycles
// NB: FIXED_CTR2 counts unhalted reference cycles
//

#ifdef WIN32

typedef BOOL (WINAPI *_InitializeOls) ();
typedef VOID (WINAPI *_DeinitializeOls) ();

typedef DWORD (WINAPI *_Rdmsr) (DWORD index, PDWORD eax, PDWORD edx);
typedef DWORD (WINAPI *_Wrmsr) (DWORD index, DWORD eax, DWORD edx);

_InitializeOls InitializeOls = NULL;
_DeinitializeOls DeinitializeOls = NULL;

_Rdmsr Rdmsr = NULL;
_Wrmsr Wrmsr = NULL;

HMODULE hModule = NULL;

//
// openPMS
//
// to get the following code to work need to do the following:
//
// for 64bit exes, make sure WinRing0x64.dll and WinRing0x64.sys are placed in the same directory
// for 32bit exes, make sure WinRing0.dll and WinRing0.sys are placed in the same directory
// exe needs to have root access eg. Run Visual Studio as Administrator
//
int openPMS()
{   
    if ((hModule = LoadLibrary(is64bitExe() ? _T("WinRing0x64.dll") : _T("WinRing0.dll"))) == NULL)
        return 0;

    int r;

    InitializeOls = (_InitializeOls) GetProcAddress(hModule, "InitializeOls");
    DeinitializeOls = (_DeinitializeOls) GetProcAddress(hModule, "DeinitializeOls");
	counter = counter + 1;
    Rdmsr = (_Rdmsr) GetProcAddress(hModule, "Rdmsr");
    Wrmsr = (_Wrmsr) GetProcAddress(hModule, "Wrmsr");
	
    if ((r = InitializeOls()) == 0) {
        cout << "unable to access performance monitoring counters" << endl;
        cout << "make sure " << (is64bitExe() ? "WinRing0x64.dll" : "WinRing0.dll") << " and " << (is64bitExe() ? "WinRing0x64.sys" : "WinRing0.sys") << " are in the same directory as sharing.exe" << endl;
        cout << "make sure sharing.exe is run as administrator " << endl << endl;
    }

    return r;
}

//
// closePMS
//
void closePMS()
{
    if (hModule) {
        DeinitializeOls();
        FreeLibrary(hModule);
        hModule = NULL;
    }
}

//
// readMSR
//
UINT64 readMSR(int cpu, int addr)
{
    DWORD high, low;
    DWORD_PTR oldAffinity = SetThreadAffinityMask(GetCurrentThread(), 1 << cpu);
    Rdmsr(addr, &low, &high);
    SetThreadAffinityMask(GetCurrentThread(), oldAffinity);
    return ((UINT64) high << 32) | low;
}

//
// writeMSR
//
void writeMSR(int cpu, int addr, UINT64 v)
{
    DWORD_PTR oldAffinity = SetThreadAffinityMask(GetCurrentThread(), 1 << cpu);
    Wrmsr(addr, (DWORD) v, v >> 32);
    SetThreadAffinityMask(GetCurrentThread(), oldAffinity);
}

#else

int *fd;

//
// openPMS
//
// to get the following code to work need to do the following:
//
// auto load msr driver on boot by adding msr to /etc/modules
// performance counters accessed by reading from and writing to /dev/cpu/n/msr where n is the cpu number
// need to have root access eg. $sudo eclipse
//
// did try the following, but it didn't work
//
// created a group called msr: $sudo groupadd msr
// added user to group: $sudo usermod -a -G msr user
// added the following code to /etc/rc.local so that after boot the /dev/cpu/n/msr files should
// be able to be read and written by users belonging to the msr group
//
// i=0
// ncpus=`cat /proc/cpuinfo | grep processor | wc -l`
// while [ $i -lt 8 ]
// do
//   chown :msr /dev/cpu/$i/msr
//   chmod ug+rwx /dev/cpu/$i/msr
//   i=`expr $i + 1`
// done
//

//
// openPMS
//

/* Aliigned memory allocation */
template <class T>
class ALIGNEDMA {

public:

    void * operator new(size_t);     // override new
    void operator delete(void*);    // override delete

};

//
// new
//
template <class T>
void* ALIGNEDMA<T>::operator new(size_t sz)
{
    return _aligned_malloc(sz, lineSz);
}

//
// delete
//
template <class T>
void ALIGNEDMA<T>::operator delete(void *p)
{
    _aligned_free(p);
}

int openPMS()
{
    char fn[32];
    int err = 0;

    ncpus = getNumberOfCPUs();

    fd = (int*) calloc(1, ncpus*sizeof(int));

    for (int i = 0; i < ncpus; i++) {
        sprintf(fn, "/dev/cpu/%d/msr", i);
        if ((fd[i] = open(fn, O_RDWR)) == -1) {
            cout << "unable to open " << fn << endl;
            err = 1;
        }
    }

    if (err) {
        cout << endl;
        cout << "make sure the msr driver is loaded by checking for file(s) /dev/cpu/0/msr, /dev/cpu/1/msr ..." << endl;
        cout << "autoload the msr driver on boot by adding msr to /etc/modules" << endl;
        cout << "make sure program is run as root" << endl;
    }
    return err == 0;
}

//
// closePMS
//
void closePMS()
{
    for (int i = 0; i < ncpus; i++) {
        if (fd[i] != -1)
            close(fd[i]);
    }
}

//
// readMSR
//
// check result returned by write to avoid a gcc warn_unused_result
//
UINT64 readMSR(int cpu, int addr)
{
    UINT64 msr = 0;
    if (fd[cpu] != -1) {
        lseek(fd[cpu], addr, SEEK_SET);
        if (read(fd[cpu], &msr, sizeof(msr)) != sizeof(msr))
            cout << "Warning: unable to readMSR(" << cpu << ", " << addr << ")" << endl;
    }
    return msr;
}

//
// writeMSR
//
// check result returned by write to avoid a gcc warn_unused_result
//
void writeMSR(int cpu, int addr, UINT64 v)
{
    if (fd[cpu] != -1) {
        lseek(fd[cpu], addr, SEEK_SET);
        if (write(fd[cpu], &v, sizeof(v)) != sizeof(v))
            cout << "Warning: unable to writeMSR(" << cpu << ", " << addr << ", " << v << ")" << endl;
    }
}

#endif

//
// pmversion
//
int pmversion()
{
    CPUID(cd, 0x0a);
    return cd.eax & 0xff;
}

//
// nfixedctr
//
int nfixedCtr()
{
    CPUID(cd, 0x0a);
    return cd.edx & 0x1f;
}

//
// fixedctrw
//
int fixedCtrW()
{
    CPUID(cd, 0x0a);
    return (cd.edx >> 5) & 0xff;
}

//
// npmc
//
int npmc()
{
    CPUID(cd, 0x0a);
    return (cd.eax >> 8 ) & 0xff;
}

//
// pmcW
//
int pmcW()
{
    CPUID(cd, 0x0a);
    return (cd.eax >> 16) & 0xff;
}

//
// readFIXED_CTR
//
UINT64 readFIXED_CTR(int cpu, int n)
{
    return readMSR(cpu, 0x0309 + n);
}

//
// writeFIXED_CTR
//
void writeFIXED_CTR(int cpu, int n, UINT64 v)
{
    return writeMSR(cpu, 0x0309 + n, v);
}

//
// readFIXED_CTR_CTRL
//
UINT64 readFIXED_CTR_CTRL(int cpu)
{
    return readMSR(cpu, 0x038d);
}

//
// writeFIXED_CTR_CTRL
//
void writeFIXED_CTR_CTRL(int cpu, UINT64 v)
{
    return writeMSR(cpu, 0x038d, v);
}

//
// readPERF_GLOBAL_STATUS
//
UINT64 readPERF_GLOBAL_STATUS(int cpu)
{
    return readMSR(cpu, 0x038e);
}

//
// writePERF_GLOBAL_STATUS
//
void writePERF_GLOBAL_STATUS(int cpu, UINT64 v)
{
    return writeMSR(cpu, 0x038e, v);
}

//
// readPERF_GLOBAL_CTRL
//
UINT64 readPERF_GLOBAL_CTRL(int cpu)
{
    return readMSR(cpu, 0x038f);
}

//
// writePERF_GLOBAL_CTRL
//
void writePERF_GLOBAL_CTRL(int cpu, UINT64 v)
{
    return writeMSR(cpu, 0x038f, v);
}

//
// readPERF_GLOBAL_OVR_CTRL
//
UINT64 readPERF_GLOBAL_OVR_CTRL(int cpu)
{
    return readMSR(cpu, 0x0390);
}

//
// writePERF_GLOBAL_OVR_CTRL
//
void writePERF_GLOBAL_OVR_CTRL(int cpu, UINT64 v)
{
    return writeMSR(cpu, 0x0390, v);
}

//
// readPERFEVTSEL
//
UINT64 readPERFEVTSEL(int cpu, int n) 
{
    return readMSR(cpu, 0x186 + n);
}

//
// writePERFEVTSEL
//
void writePERFEVTSEL(int cpu, int n, UINT64 v) 
{
    return writeMSR(cpu, 0x186 + n, v);
}

//
// readPMC
//
UINT64 readPMC(int cpu, int n)
{
    return readMSR(cpu, 0xc1 + n);
}

//
// writePMC
//
void writePMC(int cpu, int n, UINT64 v)
{
    return writeMSR(cpu, 0xc1 + n, v);
}

//
// pauseIfKeyPressed
//
void pauseIfKeyPressed()
{
#ifdef WIN32
    if (_kbhit()) {
        if (_getch() == ' ') {
            cout << endl << endl << "PAUSED - press key to continue";
            _getch();
            cout << endl;
        }
    }
#else

#endif
};

//
// quit
//
void quit(int r)
{
#ifdef WIN32
    cout << endl << "Press key to quit...";
    _getch();   // stop DOS window disappearing prematurely
#endif
    exit(r);
}

// eof