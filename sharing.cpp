//
// sharing.cpp
//
// Copyright © 2013 jones@scss.tcd.ie
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
// 19/11/12 first version
// 19/11/12 works with Win32 and x64
// 21/11/12 works with Character Set: Not Set, Unicode Character Set or Multi-Byte Character
// 21/11/12 output results so they can be easily pasted into a spreadsheet from console
// 24/12/12 increment using (0) non atomic increment (1) InterlockedIncrement64 (2) InterlockedCompareExchange
// 12/07/13 increment using (3) RTM (restricted transactional memory)
// 18/07/13 added performance counters
// 27/08/13 choice of 32 or 64 bit counters (32 bit can oveflow if run time longer than a couple of seconds)
// 28/08/13 extended struct Result
// 16/09/13 linux support (needs g++ 4.8 or later)
// 21/09/13 added getWallClockMS()
// 12/10/13 Visual Studio 2013 RC
// 12/10/13 added FALSESHARING
//

// 
// NB: hints for pasting from console window
// NB: Edit -> Select All followed by Edit -> Copy
// NB: paste into Excel using paste "Use Text Import Wizard" option and select "/" as the delimiter
//

#include "stdafx.h"                             // pre-compiled headers
#include <iostream>                             // cout
#include <iomanip>                              // setprecision
#include "helper.h"                             //

using namespace std;                            // cout

#define K           1024                        //
#define GB          (K*K*K)                     //
#define NOPS        10000                       //
#define NSECONDS    2                           // run each test for NSECONDS

//#define FALSESHARING                          // 
#define COUNTER64                               // comment for 32 bit counter

#pragma region BakeryLock
/* bakery lock */
int number[8];
int choosing[8];  
#pragma endregion BakeryLock



#ifdef COUNTER64
#define VINT    UINT64                          //
#define VLONG	volatile long long
#else
#define VINT    UINT                            //
#define VLONG	volatile long
#endif

#define ALIGNED_MALLOC(sz, align) _aligned_malloc((sz+align-1)/align*align, align)
		// Convenient and sticking to notes!!
//#define CAS(a,e,n) InterlockedCompareExchange64(a,n,e)

#ifdef FALSESHARING
#define GINDX(n)    (g+n)                       //
#else
#define GINDX(n)    (g+n*lineSz/sizeof(VINT))   //
#endif

int MAXTHREAD = 2*getNumberOfCPUs();
/*
	Allocating alligned memory space
*/
template <class T>
class ALIGNEDMA {

public:

    void * operator new(size_t);     // override new
    void operator delete(void*);    // override delete

};

/*
	New alligned memory pool 
*/
template <class T>
void* ALIGNEDMA<T>::operator new(size_t sz) // size_t is an unsigned integer which can hold an address
{
	//ALIGNED_MALLOC(sz, lineSz);
    return _aligned_malloc(sz, lineSz);
}

/*
	Delete alligned memory pool
*/
template <class T>
void ALIGNEDMA<T>::operator delete(void *p)
{
    _aligned_free(p);
}
// Derive QNode from ALIGNEDMA
// each object will be allocated its own cache line aligened on a cache line boundary
class QNode: public ALIGNEDMA<QNode> {
public:
    volatile int waiting;
    volatile QNode*next;
};


#define OPTYP      6                         // set op type
//
// 0:inc
// 1:InterlockedIncrement
// 2:InterlockedCompareExchange
// 3:RTM (restricted transactional memory)
// 4:TestAndSet Lock
// 5:TestAndTestAndSet Lock
// 6:MCS Lock
// 7:Bakery Lock



//INIT LOCK VAR
#if OPTYP == 5 || OPTYP == 4
	VLONG lock = 0;

#elif OPTYP == 6
	QNode * lock; // big mistake made here.. DO NOT DO -> QNode * lock = new QNode(); .. ma gad!
#elif OPTYP == 7
	int proc_id = 0;
#endif

#if OPTYP == 0

#define OPSTR       "inc"
#define INC(g)      (*g)++;

#elif OPTYP == 1

#ifdef COUNTER64
#define OPSTR       "InterlockedIncrement64"
#define INC(g)      InterlockedIncrement64((volatile LONG64*) g)
#else
#define OPSTR       "InterlockedIncrement"
#define INC(g)      InterlockedIncrement(g)
#endif

#elif OPTYP == 2

#ifdef COUNTER64
#define OPSTR       "InterlockedCompareExchange64"
#define INC(g)      do {                                                                        \
                        x = *g;                                                                 \
                    } while (InterlockedCompareExchange64((volatile LONG64*) g, x+1, x) != x);
#else
#define OPSTR       "InterlockedCompareExchange"
#define INC(g)      do {                                                                        \
                        x = *g;                                                                 \
                    } while (InterlockedCompareExchange(g, x+1, x) != x);
#endif

#elif OPTYP == 3

#define OPSTR       "RTM (restricted transactional memory)"
#define INC(g)      while (1) {                                                                 \
                        int status = _xbegin();                                                 \
                        if (status == _XBEGIN_STARTED) {                                        \
                            (*g)++;                                                             \
                            _xend();                                                            \
                            break;                                                              \
                        }                                                                       \
                    }
#elif OPTYP == 4
#define OPSTR		"TestAndSet Lock" 
#define INC(g)      while(InterlockedExchange64(&lock,1));										\
					(*g)++;																		\
					lock = 0;

#elif OPTYP == 5
#define OPSTR		"TestAndTestAndSet Lock"
#define INC(g)		while(InterlockedExchange64(&lock,1))										\
						while(lock == 1)														\
							_mm_pause();														\
					(*g)++;																		\
					lock = 0;

#elif OPTYP == 6
#define OPSTR       "MCS Lock"
//#define INC(g)
#define INC(g)      acquire(&lock);                         \
                    (*g)++;                                 \
                    release(&lock);   
DWORD tlsIndex;
inline void acquire(QNode**lock){
	
    volatile QNode *qn = (QNode*) TlsGetValue(tlsIndex);
    qn->next = NULL;
    volatile QNode * pred= (QNode*) InterlockedExchangePointer((PVOID*) lock, (PVOID) qn);
    if(pred== NULL)
        return;
    qn->waiting = 1;
    pred->next = qn;
    while(qn->waiting)
		Sleep(0);
}
inline void release(QNode**lock) {
    volatile QNode *qn = (QNode*) TlsGetValue(tlsIndex);
    volatile QNode * succ;
    if(!(succ = qn->next)) {
        if(InterlockedCompareExchangePointer((PVOID*)lock,NULL,(PVOID)qn) == qn)
            return;
        do {
            succ= qn->next;
        }while(!succ);
    }
    succ->waiting = 0;
}
    
#elif OPTYP == 7
#define OPSTR		"Bakery Lock"
#define DECLARE()	int p_id
#define INC(g)		acquire();					\
					_mm_mfence();					\
					(*g)++;							\
					release_lock(p_id);				\
					_mm_lfence();
inline void acquire(int pid) {
	choosing[pid] = 1;
	int max =0;
	for(int i = 0; i < 8; i++) {
		if(number[i] > max)
			max = number[i];
	}
	number[pid] = max + 1;
	choosing[pid] = 0;
	for(int  j = 0; j < 8; j++) {
		while(choosing[j]);
	//	cout << "j = " << j << "\tnumber[j] = "<<number[j]<<"\tnumber[pid]" << number[pid] << endl;
		while((number[j]!=0) && ((number[j] < number[pid]) || ((number[j] == number[pid]) && (j <pid)))){
			_mm_lfence();
		}
	}
}
inline void release_lock(int pid) {
//	cout << "Releasing number["<<pid<<"] which = " << number[pid] << endl;
	number[pid] = 0;
	_mm_lfence();
//	printf("Released ticket %d\n" , number[pid]);
}
#endif

UINT64 tstart;                                  // start of test in ms
int sharing;                                    // % sharing
int lineSz;                                     // cache line size
UINT64 s_counter = 0;
int maxThread;                                  // max # of threads

THREADH *threadH;                               // thread handles
UINT64 *cnt;                                    // for computing results

typedef struct {
    int sharing;                                // sharing
    int nt;                                     // # threads
    UINT64 rt;                                  // run time (ms)
    UINT64 diff;                                // diff (should be 0)
    UINT64 *cnt;                                // ops for each thread
} Result;

Result *r;                                      // results

int indx;                                       // results index

volatile VINT *g;                               // NB: position of volatile

UINT64 *fixedCtr0;                              // fixed counter 0 counts
UINT64 *fixedCtr1;                              // fixed counter 1 counts
UINT64 *fixedCtr2;                              // fixed counter 2 counts
UINT64 *pmc0;                                   // performance counter 0 counts
UINT64 *pmc1;                                   // performance counter 1 counts
UINT64 *pmc2;                                   // performance counter 2 counts
UINT64 *pmc3;                                   // performance counter 2 counts

//
// test memory allocation [see lecture notes]
//
__declspec(align(64)) UINT64 cnt0;
__declspec(align(64)) UINT64 cnt1;
__declspec(align(64)) UINT64 cnt2; 
UINT64 cnt3;

//
// zeroCounters
//
void zeroCounters()
{
    for (int i = 0; i < ncpus; i++) {
        for (int j = 0; j < 4; j++) {
            if (j < 3)
                writeFIXED_CTR(i, j, 0);
            writePMC(i, j, 0);
        }
    }   
}

//
// void setupCounters()
//
void setupCounters()
{
    if (!openPMS())
        quit();

    //
    // enable FIXED counters
    //
    for (int i = 0; i < ncpus; i++) {
        writeFIXED_CTR_CTRL(i, (FIXED_CTR_RING123 << 8) | (FIXED_CTR_RING123 << 4) | FIXED_CTR_RING123);
        writePERF_GLOBAL_CTRL(i, (0x07ULL << 32) | 0x0f);
    }

#if OPTYP == 3

    //
    // set up and enable general purpose counters
    //
    for (int i = 0; i < ncpus; i++) {
        writePERFEVTSEL(i, 0, PERFEVTSEL_EN | PERFEVTSEL_USR | RTM_RETIRED_START);
        writePERFEVTSEL(i, 1, PERFEVTSEL_EN | PERFEVTSEL_USR | RTM_RETIRED_COMMIT);
        writePERFEVTSEL(i, 2, PERFEVTSEL_IN_TXCP | PERFEVTSEL_IN_TX | PERFEVTSEL_EN | PERFEVTSEL_USR | CPU_CLK_UNHALTED_THREAD_P);  // NB: TXCP in PMC2 ONLY
        writePERFEVTSEL(i, 3, PERFEVTSEL_IN_TX | PERFEVTSEL_EN | PERFEVTSEL_USR | CPU_CLK_UNHALTED_THREAD_P);
    }

#endif

}

//
// void saveCounters()
//
void saveCounters()
{
    for (int i = 0; i < ncpus; i++) {
        fixedCtr0[indx*ncpus + i] = readFIXED_CTR(i, 0);
        fixedCtr1[indx*ncpus + i] = readFIXED_CTR(i, 1);
        fixedCtr2[indx*ncpus + i] = readFIXED_CTR(i, 2);
        pmc0[indx*ncpus + i] = readPMC(i, 0);
        pmc1[indx*ncpus + i] = readPMC(i, 1);
        pmc2[indx*ncpus + i] = readPMC(i, 2);
        pmc3[indx*ncpus + i] = readPMC(i, 3);
    }

}





//
// worker
//
WORKER worker(void *vthread)
{
    int thread = (int)((size_t) vthread);
    UINT64 ops = 0;
    int pid = thread;
    volatile VINT *gt = GINDX(thread);
    volatile VINT *gs = GINDX(maxThread);

#if OPTYP == 2
    VINT x;
#elif OPTYP == 6
    QNode * q = new QNode();
	q->next = NULL;
	q->waiting = 0;
    TlsSetValue(tlsIndex, q);
#elif OPTYP == 7
	DECLARE() = thread;
#endif
    runThreadOnCPU(thread % ncpus);
    while (1) {
        //
        // do some work
        //
        for (int i = 0; i < NOPS / 4; i++) {

            switch (sharing) {
            case 0:
                INC(gt);
                INC(gt);
                INC(gt);
                INC(gt);
                break;

            case 25:
                INC(gt);
                INC(gt);
                INC(gt);
                INC(gs);
                break;

            case 50:
                INC(gt);
                INC(gs);
                INC(gt);
                INC(gs);
                break;
            
            case 75:
                INC(gt);
                INC(gs);
                INC(gs);
                INC(gs);
                break;

            case 100:
                INC(gs);
                INC(gs);
                INC(gs);
                INC(gs);
				break;
			default:
				break;
            }

        }
		
        ops += NOPS;

        //
        // check if runtime exceeded
        //
        if ((getWallClockMS() - tstart) > NSECONDS*1000)
            break;
    }
    cnt[thread] = ops;
    return 0;

}

//
// main
//
int main()
{
    ncpus = getNumberOfCPUs();  // # of logical CPUs
    maxThread = 2 * ncpus;      // max # threads to run

    //
    // get date
    //
    char dateAndTime[256];
    getDateAndTime(dateAndTime, sizeof(dateAndTime));
    
    //
    // console output
    //
    cout << getHostName() << " " << getOSName() << " sharing " << (is64bitExe() ? "(64" : "(32") << "bit EXE) " << OPSTR;
#ifdef _DEBUG
    cout << " DEBUG";
#else
    cout << " RELEASE";
#endif
#ifdef COUNTER64
    cout << " COUNTER64";
#else
    cout << " COUNTER32";
#endif
#ifdef FALSESHARING
    cout << " FALSESHARING";
#endif
    cout << " NCPUS=" << ncpus << " RAM=" << (getPhysicalMemSz() + GB - 1) / GB << "GB NOPS=" << NOPS << " " << dateAndTime << endl;
    cout << "Intel" << (cpu64bit() ? "64" : "32") << " family " << cpuFamily() << " model " << cpuModel() << " stepping " << cpuStepping() << " " << cpuBrandString() << endl;
    cout << "performance monitoring version " << pmversion() << ", " << nfixedCtr() << " x " << fixedCtrW() << "bit fixed counters, " << npmc() << " x " << pmcW() << "bit performance counters" << endl;

    //
    // get cache info
    //
    lineSz = getCacheLineSz();
    //lineSz *= 2;

    if ((&cnt3 >= &cnt0) && (&cnt3 < (&cnt0 + lineSz/sizeof(UINT64))))
        cout << "Warning: cnt3 shares cache line used by cnt0" << endl;
    if ((&cnt3 >= &cnt1) && (&cnt3 < (&cnt1 + lineSz / sizeof(UINT64))))
        cout << "Warning: cnt3 shares cache line used by cnt1" << endl;
    if ((&cnt3 >= &cnt2) && (&cnt3 < (&cnt2 + lineSz / sizeof(UINT64))))
        cout << "Warning: cnt2 shares cache line used by cnt1" << endl;

#if OPTYP == 3

    //
    // check if RTM supported
    //
    if (!rtmSupported()) {
        cout << "RTM (restricted transactional memory) NOT supported by this CPU" << endl;
        quit();
        return 1;
    }

#endif
#if OPTYP == 6
	tlsIndex = TlsAlloc();
#endif

    cout << endl;

    //
    // allocate global variable
    //
    // NB: each element in g is stored in a different cache line to stop false sharing
    //
    threadH = (THREADH*) ALIGNED_MALLOC(maxThread*sizeof(THREADH), lineSz);             // thread handles
    cnt = (UINT64*) ALIGNED_MALLOC(maxThread*sizeof(UINT64), lineSz);                   // for computing ops/s

#ifdef FALSESHARING
    g = (VINT*) ALIGNED_MALLOC((maxThread+1)*sizeof(VINT), lineSz);                     // local and shared global variables
#else
    g = (VINT*)ALIGNED_MALLOC((maxThread + 1)*lineSz, lineSz);                          // local and shared global variables
#endif

    fixedCtr0 = (UINT64*) ALIGNED_MALLOC(5*maxThread*ncpus*sizeof(UINT64), lineSz);     // for fixed counter 0 results
    fixedCtr1 = (UINT64*) ALIGNED_MALLOC(5*maxThread*ncpus*sizeof(UINT64), lineSz);     // for fixed counter 1 results
    fixedCtr2 = (UINT64*) ALIGNED_MALLOC(5*maxThread*ncpus*sizeof(UINT64), lineSz);     // for fixed counter 2 results
    pmc0 = (UINT64*) ALIGNED_MALLOC(5*maxThread*ncpus*sizeof(UINT64), lineSz);          // for performance counter 0 results
    pmc1 = (UINT64*) ALIGNED_MALLOC(5*maxThread*ncpus*sizeof(UINT64), lineSz);          // for performance counter 1 results
    pmc2 = (UINT64*) ALIGNED_MALLOC(5*maxThread*ncpus*sizeof(UINT64), lineSz);          // for performance counter 2 results
    pmc3 = (UINT64*) ALIGNED_MALLOC(5*maxThread*ncpus*sizeof(UINT64), lineSz);          // for performance counter 3 results
    
    r = (Result*) ALIGNED_MALLOC(5*maxThread*sizeof(Result), lineSz);                   // for results
    memset(r, 0, 5*maxThread*sizeof(Result));                                           // zero
    for (int i = 0; i < 5*maxThread; i++) {                                             //
        r[i].cnt = (UINT64*) ALIGNED_MALLOC(maxThread*sizeof(UINT64), lineSz);          // for results
        memset(r[i].cnt, 0, maxThread*sizeof(UINT64));                                  //
    }                                                                                   //

    indx = 0;

    //
    // set up performance monitor counters
    //
    setupCounters();

    //
    // boost process priority
    // boost current thread priority to make sure all threads created before they start to run
    //
#ifdef WIN32
    SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#endif
    //
    // run tests
    //
    UINT64 ops1 = 1;

    for (sharing = 0; sharing <= 100; sharing += 25) {
		
		cout << "sharing : "<<sharing<<endl;
        for (int nt = 1; nt <= maxThread; nt *= 2, indx++) {
            
            //
            //  zero shared memory
            //
            for (int thread = 0; thread < nt; thread++)
                *(GINDX(thread)) = 0;   // thread local
            *(GINDX(maxThread)) = 0;    // shared

            //
            // zero counters
            //
            zeroCounters();

            //
            // get start time
            //
            tstart = getWallClockMS();

            //
            // create worker threads
            //
            for (int thread = 0; thread < nt; thread++){
				
				
				createThread(&threadH[thread], worker, (void*)(size_t)thread);
				//printf("Thread finished %d\n", thread);
				
			}
            //
            // wait for ALL worker threads to finish
            //
            waitForThreadsToFinish(nt, threadH);
            UINT64 rt = getWallClockMS() - tstart;
            
            saveCounters();

            //
            // save results and output summary to console
            //
            UINT64 total = 0, incs = 0;
            for (int thread = 0; thread < nt; thread++) {
                r[indx].cnt[thread] = cnt[thread];
                total += cnt[thread];
                incs += *(GINDX(thread));
            }
            incs += *(GINDX(maxThread));
            if ((sharing == 0) && (nt == 1))
                ops1 = total;
            r[indx].sharing = sharing;
            r[indx].nt = nt;
            r[indx].rt = rt;
            r[indx].diff = total - incs;

            cout << "sharing= " << setw(3) << sharing << "% threads= " << setw(2) << nt;
            cout << " rt= " << setw(5) << fixed << setprecision(2) << (double) rt / 1000;
            cout << " ops= " << setw(11) << total << " relative= " << setw(5) << fixed << setprecision(2) << (double) total / ops1;

#if OPTYP == 3

            UINT64 start = 0;
            UINT64 commit = 0;

            for (int i = 0; i < ncpus; i++) {
                start += pmc0[indx*ncpus + i];
                commit += pmc1[indx*ncpus + i];
            }
            cout << " RTM commit= " << setw(3) << fixed << setprecision(0) << 100.0 * commit/start << "%"; 

#endif

            if (total == incs) {
                cout << " OK";
            } else {
                cout << " ERROR incs= " << setw(10) << incs << " diff= " << setw(10) << total - incs;
            }

            cout << endl;

            //
            // delete thread handles
            //
            for (int thread = 0; thread < nt; thread++)
                closeThread(threadH[thread]);

        }

    }

    cout << endl;
    
    //
    // output results so they can easily be pasted into a spread sheet from console window
    //
    cout << "sharing, nt, rt, diff, ops for each thread" << endl; 
    for (int i = 0; i < indx; i++) {
        cout << r[i].sharing << "/"  << r[i].nt << "/" << r[i].rt << "/"  << r[i].diff;
        for (int j = 0; j < maxThread; j++)
            cout << "/" << r[i].cnt[j];
        cout << endl;
    }
    cout << endl;
    cout << "FIXED_CTR0 instructions retired" << endl;
    for (int i = 0; i < indx; i++) {
        for (int j = 0; j < ncpus; j++)
            cout << ((j) ? "/" : "") << fixedCtr0[i*ncpus + j];
        cout << endl;
    }
    cout << endl;
    cout << "FIXED_CTR1 unhalted core cycles" << endl;
    for (int i = 0; i < indx; i++) {
        for (int j = 0; j < ncpus; j++)
            cout << ((j) ? "/" : "") << fixedCtr1[i*ncpus + j];
        cout << endl;
    }
    cout << endl;
    cout << "FIXED_CTR2 unhalted reference cycles" << endl;
    for (int i = 0; i < indx; i++) {
        for (int j = 0; j < ncpus; j++ )
            cout << ((j) ? "/" : "") << fixedCtr2[i*ncpus + j];
        cout << endl;
    }
    cout << endl;
    cout << "PMC0 RTM RETIRED START" << endl;
    for (int i = 0; i < indx; i++) {
        for (int j = 0; j < ncpus; j++ )
            cout << ((j) ? "/" : "") << pmc0[i*ncpus + j];
        cout << endl;
    }
    cout << endl;
    cout << "PMC1 RTM RETIRED COMMIT" << endl;
    for (int i = 0; i < indx; i++) {
        for (int j = 0; j < ncpus; j++ )
            cout << ((j) ? "/" : "") << pmc1[i*ncpus + j];
        cout << endl;
    }
    cout << endl;
    cout << "PMC2 unhalted core cycles in committed transactions" << endl;
    for (int i = 0; i < indx; i++) {
        for (int j = 0; j < ncpus; j++ )
            cout << ((j) ? "/" : "") << pmc2[i*ncpus + j];
        cout << endl;
    }
    cout << endl;
    cout << "PMC3 unhalted core cycles in committed and aborted transactions" << endl;
    for (int i = 0; i < indx; i++) {
        for (int j = 0; j < ncpus; j++ )
            cout << ((j) ? "/" : "") << pmc3[i*ncpus + j];
        cout << endl;
    }
	cout << "counter = " << s_counter << endl;
    closePMS();

    quit();

    return 0;

}

// eof