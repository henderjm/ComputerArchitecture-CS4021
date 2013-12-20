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
int nt;
#define H (2 * nt)
#define R (100 + 2*H)
THREADH *threadH;                               // thread handles
UINT64 * threadts;

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
volatile int nmallocs;
int * nodes;
volatile long long lock = 0;


typedef struct {
	int sharing;                                // sharing
	int nt;                                     // # threads
	UINT64 rt;                                  // run time (ms)
	UINT64 diff;                                // diff (should be 0)
	UINT64 mallocs;								// nmallocs
	UINT64 *cnt;                                // ops for each thread
	UINT64 mkeys;
	UINT64 nmalls;
	UINT64 *nnodes;
	UINT64 nnodes2;
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
	//	nmallocs++;
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
	volatile UINT64 key;
	Node * next;
	Node * link;
	Node(UINT64, Node*);
};


Node::Node(UINT64 k, Node * n) {
	this->key = k;
	this->next = n;
	this->link = NULL;
}

#pragma endregion CLASS_NODE

#pragma region CLASS_LIST

class List : public ALIGNEDMA<List> {
public:
	List();
	~List();
	int add(UINT64);
	int remove(UINT64);
	volatile int count(int);
	Node * volatile head;
private:

	int find(UINT64, Node* volatile *&, Node * &, Node * &);
};
List *retire; List *reuse;
List::List() {
	head = new Node(0, NULL);
	nmallocs++;
}

List::~List() {
	while (head) {
		Node *tmp = head->next;
		delete head;
		head = tmp;
	}
}

int List::add(UINT64 key) {
	Node *volatile *pred,*curr,*succ, *newNode = NULL;
	//	nmallocs++;
	do {
		if(find(key, pred, curr, succ)) {// curr and pred will be unmarked
			if(newNode)
				delete newNode;
			return 0;
		}
		if (newNode== NULL){
			newNode= new Node(key, curr);
			nmallocs++;
		}
		//		newNode->next = curr;
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

int List::remove(UINT64 key) {
	Node * volatile * pred;
	Node * curr, * succ;
	//	nmallocs += 3;
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
		
	//	succ->next->key = __rdtsc();
		return 1;
	}
}

int List::find(UINT64 k, Node* volatile*&pred, Node *&curr, Node *&succ) {
retry:
	pred = &head;
	curr = *pred;
	while(curr) { // iterate through list
		succ =  curr->next;
		if(ISMARKED(succ)) { // remove

			if(CAS((volatile PVOID*) pred, curr, UNMARKED(succ)) != curr) {
				curr->key = __rdtsc();
				retire->add(curr->key); // adding to retireQ
				goto retry;
			}
			//				// deferred deletion
			//    		retire->add(succ->key); // adding to retireQ
			curr = UNMARKED(succ);
		} else {
			int ckey = curr->key; // copied key
			if(*pred != curr)
				goto retry;
			if(ckey >= k) {
				return (ckey == k);
			}
			pred = &curr->next;
			curr = succ;
		}
	}

	return 0;
}

/* Pretty Print List */
volatile int List::count(int s) {
	Node * iter = head;
	//	printf("blah\t%d\n", iter->next->key);
	if(iter->next == NULL) {
		return 1;
	}
	/*	for(iter; iter != NULL; iter = iter->next) {
	s++;
	if(iter->next == NULL)
	return s;
	}*/

	while(iter) {
		s++;
		iter = iter->next;
	}

	return s;
}

#pragma endregion CLASS_LIST

int acquire(volatile long long &lock, List * l) {
	while(InterlockedExchange64(&lock,1))										
		while(lock == 1)													
			_mm_pause();
	int c = 0;
	c = l->count(c);
	lock = 0;
	return c;
}
#pragma region HAZARD
// Hazard pointer record
/*class HPRecType {
HPRecType();
~HPRecType();
Node * HP[2];
HPRecType * headHPRec;
HPRecType * next;
void retireNode(Node * n);
private:
List rlist;
int rcount;
};
HPRecType::HPRecType() {
rlist.head = NULL;
rcount = 0;
}

void HPRecType::retireNode(Node * node) {
rlist.add(node->key);
rcount++;
if(rcount >= R) 
Scan(headHPRec);
}

void Scan(HPRecType *) {
}*/

#pragma endregion HAZARD
//HPRecType * hp;

List *list;

UINT64 * size; // size of lists
WORKER worker(void *vthread) {
	int thread = (int)((size_t) vthread);
	retire = new List();
	reuse = new List(); // pair of to-be-freed lists for each thread.
	UINT64 ops;
	int ns = 0;
	int list_size = 0;
	nmallocs = 0;
	srand(thread);
	runThreadOnCPU(thread % ncpus);
	try { // avoiding out of memory!

		do {
			for(int i = 0; i < NOPS / 4; i++) {
				threadts[thread] = __rdtsc();

				int new_key = rand() % (maxkey * 2); // multiply maxkey * 2 so on >> 1, back to inside range
				/*
				if(new_key & 1) list->add(new_key);
				else list->remove(new_key);
				*/

				new_key & 1 ? list->add(new_key >> 1) : list->remove(new_key >> 1); // sweet!
				_mm_mfence();
				//		if(new_key & 17000) { printf("List size: %d\n", ns ); ns++; }


			}
		}while(!((getWallClockMS() - tstart) > NSECONDS *1000));
	} catch (std::bad_alloc) {
		perror("bad memory");
	}
	//	list_size = acquire(lock, list);
	size[thread] = ns;
	//	release(&lock);
	//	cnt[thread] = ops;
	return 0;
}
// MAIN
int main() {

	// QUICKTEST LIST
	list = new List();

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
	nodes = (int*) malloc(maxThread*sizeof(int));
	threadH = (THREADH*) ALIGNED_MALLOC(maxThread*sizeof(THREADH), lineSz);             // thread handles
	cnt = (UINT64*) ALIGNED_MALLOC(maxThread*sizeof(UINT64), lineSz);	// calculate ops/s
	size = (UINT64*) ALIGNED_MALLOC(maxThread*sizeof(UINT64), lineSz);
	delete list;
	Result * r = (Result*) ALIGNED_MALLOC(5*maxThread*sizeof(Result), lineSz);
	memset(r,0,5*maxThread*sizeof(Result));
	for (int i = 0; i < 5*maxThread; i++) {                                             //
		r[i].cnt = (UINT64*) ALIGNED_MALLOC(maxThread*sizeof(UINT64), lineSz);          // for results
		memset(r[i].cnt, 0, maxThread*sizeof(UINT64));                                  //
		r[i].nnodes = (UINT64*) ALIGNED_MALLOC(maxThread*sizeof(UINT64), lineSz);          // for results
		memset(r[i].nnodes, 0, maxThread*sizeof(UINT64));
	}   

	for(maxkey = 64; maxkey <= MAXKEY; maxkey *= 4) {

		for(nt = 1; nt <= maxThread; nt *= 2, indx++) {

			list = new List();
			int l_size = 0;
			threadts = (UINT64*) ALIGNED_MALLOC(nt*sizeof(UINT64), lineSz);
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
			l_size = acquire(lock, list);
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
				r[indx].nmalls = nmallocs;
				r[indx].nnodes[thread] = size[thread];
				r[indx].nnodes2 = l_size;
				r[indx].mkeys = maxkey;
				total += size[thread];
			}
			cout << "maxkey= " << setw(3) << maxkey << " threads= " << setw(2) << nt;
			cout << " mallocs= " << setw(5) << nmallocs;
			cout << " listQ= " << setw(5) << l_size << endl;
			//
			// delete thread handles
			//
			for (int thread = 0; thread < nt; thread++)
				closeThread(threadH[thread]);
			// PRINT RESULTS
			//			r[indx].mkeys = maxkey;
			//			r[indx].nmalls = nmallocs;
			//			r[indx].nnodes = nodes;
			//		printf("\nMAXKEY: %d\tthreads: %d\tMallocs: %d\tNodes: %d\n", maxkey, nt,nmallocs,nodes);
			//		std::cout << " ops= " << setw(11) << total << " relative= " << setw(5) << fixed << setprecision(2) << (double) total << std::endl;
			delete list;
			//	delete retire;
		}
	}
	/*cout << "mallocs, sharing, nt, rt, diff, ops for each thread" << endl; 
	for (int i = 0; i < indx; i++) {
	cout <<  r[i].mallocs<< r[i].sharing << "/"  << r[i].nt << "/" << r[i].rt << "/"  << r[i].diff ;
	for (int j = 0; j < maxThread; j++)
	cout << "/" << r[i].cnt[j];
	cout << endl;
	}*/
	cout << "maxkeys / size / malls / nodes" << endl;
	for (int i = 0; i < indx; i++) {
		cout << r[i].mkeys << '/' << r[i].nnodes2 << '/' << r[i].nmalls;

		cout << endl;
	}
	printf("list\n");

	quit();

	return 0;
}