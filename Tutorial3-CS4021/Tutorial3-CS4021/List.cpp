#include "stdafx.h"
#include "List.h"
#include "stdafx.h"                             //
#include "time.h"                               // time
#include "conio.h"                              // _getch
#include "intrin.h"                             // intrinsics
#include <iostream>                             // cout
#include <iomanip>                              // setprecision
#include "helper.h"
#include <Windows.h>
#include <atomic>
#include "Node.h"

using namespace std;

#define K           1024                        //
#define GB          (K*K*K)                     //
#define NOPS        10000                       //
#define NSECONDS    2                           // run each test for NSECONDS
#define OPTYP		0							// type of lock
#define OPSTR		"Lock Algorithm"			// *************change this*****************

#define ISMARKED(n) ((size_t) n & 1) // NB: size_t
#define MARKED(n) ((Node*) ((size_t) n | 1))// NB: size_t
#define UNMARKED(n) ((Node*) ((size_t) n & ~1))// NB: size_t
#define MAXKEY 16384

THREADH *threadH;                               // thread handles
#define ALIGNED_MALLOC(sz, align) _aligned_malloc((sz+align-1)/align*align, align)
#define CAS(a,e,n) InterlockedCompareExchangePointer(a,n,e)

__declspec(align(64)) UINT64 cnt0;
__declspec(align(64)) UINT64 cnt1;
__declspec(align(64)) UINT64 cnt2; 
UINT64 * cnt; // calculate ops 
UINT64 cnt3;
int lineSz;
UINT64 tstart;                                  // start of test in ms
int maxkey;
int nmallocs;
int nodes;

typedef struct {
	int sharing;                                // sharing
	int nt;                                     // # threads
	UINT64 rt;                                  // run time (ms)
	UINT64 diff;                                // diff (should be 0)
	UINT64 mallocs;								// nmallocs
	UINT64 *cnt;                                // ops for each thread
	UINT64 mkeys;
	UINT64 nmalls;
	UINT64 nnodes;
} Result;
int indx;
#pragma region MEMORY_POOL
template <class T>
class ALIGNEDMA {

public:

	void * operator new(size_t);     // override new
	void operator delete(void*);    // override delete

};
template <class T>
void* ALIGNEDMA<T>::operator new(size_t sz) // size_t is an unsigned integer which can hold an address
{
	//ALIGNED_MALLOC(sz, lineSz);
	nmallocs++;
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

#pragma endregion MEMORY_POOL

#pragma region CLASS_NODE

class Node : public ALIGNEDMA<Node> {
public:
	volatile int key;
	Node * next;
	Node(int, Node*);
};


Node::Node(int k, Node * n) {
	this->key = k;
	this->next = n;
}

#pragma endregion CLASS_NODE

#pragma region CLASS_LIST

class List : public ALIGNEDMA<Node> {
public:
	List();
	~List();
	int add(int);
	int remove(int);
	Node * volatile head;
private:

	int find(int, Node* volatile *&, Node * &, Node * &);
};

List::List() {
	head = new Node(0, NULL);
	nmallocs++;
}

List::~List() {
	Node * temp;// = head->next;
	while(temp = head->next) {
		delete head;
		head = temp;
	}
}

int List::add(int key) {
	Node *volatile *pred,*curr,*succ, *newNode = NULL;
	nmallocs+=4;
	do {
		if(find(key, pred, curr, succ)) {// curr and pred will be unmarked
			if(newNode)
				delete newNode;
			return 0;
		}
		if (newNode== NULL){
			newNode= new Node(key, NULL);
			nmallocs++;
		}
		newNode->next = curr;
		//		InterlockedCompareExchangePointer((volatile PVOID*)pred, newNode, (PVOID) curr) == curr ? ret = true : ret = false;
		if(CAS((volatile PVOID*)pred, (PVOID) curr, newNode) == curr){
			nodes++;
			return 1;
		}
		//	printf("stuck\n");
		//		if(CAS((volatile PVOID*)pred, (PVOID) curr, newNode) == curr)
		//			return 1;
	} while(1);

	return 1;
}

int List::remove(int key) {
	Node * volatile * pred;
	Node * curr, * succ;
	nmallocs += 3;
	while(true) {
		if(find(key, pred, curr, succ) == 0) {
			return 0;
		}
		if(CAS((volatile PVOID*) &curr->next, succ, MARKED(succ)) != succ) {
			continue;
		}
		if(CAS((volatile PVOID*) pred, curr, succ) != curr) {
			find(key, pred, curr, succ);
		}
		return 1;
	}
}

int List::find(int k, Node * volatile *& pred, Node * &curr, Node * &succ) {
retry:
	pred = &head;
	curr = *pred;
	while(curr) { // iterate through list
		succ =  curr->next;
		if(ISMARKED(succ)) { // remove
			if(CAS((volatile PVOID*) pred, curr, UNMARKED(succ)) != curr)
				goto retry;
			curr = UNMARKED(succ);
		} else {
			int ckey = curr->key; // copied key
			// [TODO] make sure hasnt been changed
			if(ckey >= k) {
				return (ckey == k);
			}
			pred = &curr->next;
			curr = succ;
		}
	}

	return 0;
}

#pragma endregion CLASS_LIST

/* Pretty Print List */
void prettyprint(List *l) {
	Node * iter = l->head;
	printf("blah\t%d\n", iter->next->key);
	for(iter; iter != NULL; iter = iter->next) {
		nodes++;
	}
}

List *list;

WORKER worker(void *vthread) {
	int thread = (int)((size_t) vthread);
	UINT64 ops;
	srand(thread);
	runThreadOnCPU(thread % ncpus);
	try { // avoiding out of memory!

		do {
			for(int i = 0; i < NOPS / 4; i++) {
				int new_key = rand() % (maxkey * 2); // multiply maxkey * 2 so on >> 1, back to inside range
				/*
				if(new_key & 1) list->add(new_key);
				else list->remove(new_key);
				*/

				new_key & 1 ? list->add(new_key >> 1) : list->remove(new_key >> 1); // sweet!
				if(new_key & 1) printf("Operation added: %d\n", new_key >> 1);
//				else printf("Operation removed: %d\n", new_key >> 1);
			}
//			ops += NOPS;
		}while(!((getWallClockMS() - tstart) > NSECONDS *1000));
	} catch (std::bad_alloc) {
		perror("bad memory");
	}
//	cnt[thread] = ops;
	return 0;
}
// MAIN
int main() {

	// QUICKTEST LIST
	list = new List();
	list->add(2);
	list->add(1);
	list->add(5);
	list->add(4);
	list->remove(2);
	list->remove(0);
	list->add(4);
	list->remove(3);
	// QUICKTEST END
	ncpus = getNumberOfCPUs();  // # of logical CPUs
	int maxThread = 2* ncpus;      // max # threads to run
	indx = 0;
	//
	// get date
	//
	char dateAndTime[256];
	getDateAndTime(dateAndTime, sizeof(dateAndTime));
	//
	// console output
	//
	std::cout << getHostName() << " " << getOSName() << " sharing " << (is64bitExe() ? "(64" : "(32") << "bit EXE) " << OPSTR;
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
	lineSz = getCacheLineSz();

	if ((&cnt3 >= &cnt0) && (&cnt3 < (&cnt0 + lineSz/sizeof(UINT64))))
		cout << "Warning: cnt3 shares cache line used by cnt0" << endl;
	if ((&cnt3 >= &cnt1) && (&cnt3 < (&cnt1 + lineSz / sizeof(UINT64))))
		cout << "Warning: cnt3 shares cache line used by cnt1" << endl;
	if ((&cnt3 >= &cnt2) && (&cnt3 < (&cnt2 + lineSz / sizeof(UINT64))))
		cout << "Warning: cnt2 shares cache line used by cnt1" << endl;

	threadH = (THREADH*) ALIGNED_MALLOC(maxThread*sizeof(THREADH), lineSz);             // thread handles
	cnt = (UINT64*) ALIGNED_MALLOC(maxThread*sizeof(UINT64), lineSz);	// calculate ops/s
	delete list;
	Result * r = (Result*) ALIGNED_MALLOC(5*maxThread*sizeof(Result), lineSz);
	memset(r,0,5*maxThread*sizeof(Result));
	for (int i = 0; i < 5*maxThread; i++) {                                             //
		r[i].cnt = (UINT64*) ALIGNED_MALLOC(maxThread*sizeof(UINT64), lineSz);          // for results
		memset(r[i].cnt, 0, maxThread*sizeof(UINT64));                                  //
	}   

	for(maxkey = 16; maxkey <= MAXKEY; maxkey *= 4) {
		
		for(int nt = 1; nt <= maxThread; nt *= 2, indx++) {

			list = new List();
			nmallocs = 0;
			nodes = 0;
			//
			// get start time
			//
			tstart = clock();

			//
			// create worker threads
			//
			for (int thread = 0; thread < nt; thread++)
				createThread(&threadH[thread], worker, (void*)(size_t)thread);

			//
			// wait for ALL worker threads to finish
			//
			waitForThreadsToFinish(nt, threadH);
			int runtime = clock() - tstart;

			//
			// output results summary on console
			//
			long long total = 0;
			//            for (int thread = 0; thread < nt; thread++)
			//                total += cnt[thread];
			//            long long opspersec = 1000 * total / runtime;
			//            if (nt == 1)
			//                opspersec1 = opspersec;
			//            r[ri++] = opspersec;
			//            double drt = double(runtime) / 1000.0;
			//            cout << "key=" << setw(5) << maxkey << " threads=" << setw(2) << nt;
			//            cout << " rt=" << setw(5) << fixed << setprecision(2) << drt;
			//           cout << " ops/s=" << setw(8) << opspersec << " relative=" << fixed << setprecision(2) << (double) opspersec / opspersec1 << endl;
			for (int thread = 0; thread < nt; thread++) {
				r[indx].cnt[thread] = cnt[thread];
				r[indx].mallocs = nmallocs;
				total += cnt[thread];
			}
			//
			// delete thread handles
			//
			for (int thread = 0; thread < nt; thread++)
				closeThread(threadH[thread]);
			// PRINT RESULTS
			r[indx].mkeys = maxkey;
			r[indx].nmalls = nmallocs;
			r[indx].nnodes = nodes;
			printf("\nMAXKEY: %d\tthreads: %d\tMallocs: %d\tNodes: %d\n", maxkey, nt,nmallocs,nodes);
			std::cout << " ops= " << setw(11) << total << " relative= " << setw(5) << fixed << setprecision(2) << (double) total << std::endl;
			list->~List();
		}
	}
	/*cout << "mallocs, sharing, nt, rt, diff, ops for each thread" << endl; 
	for (int i = 0; i < indx; i++) {
		cout <<  r[i].mallocs<< r[i].sharing << "/"  << r[i].nt << "/" << r[i].rt << "/"  << r[i].diff ;
		for (int j = 0; j < maxThread; j++)
			cout << "/" << r[i].cnt[j];
		cout << endl;
	}*/
	cout << "malls / nodes" << endl;
	for (int i = 0; i < indx; i++) {
		cout << r[i].nmalls << "/" << r[i].nnodes << endl;
	}
	printf("list\n");

	quit();

	return 0;
}