#include <iostream>
#include <stdio.h>
#include <cstdlib>
#include <sstream>
#include <string>
#include <list>
#include <algorithm>
#include <sys/time.h>
#include <pthread.h>
#include "mano.h"

#define NUM_THREADS 4
#define HANDS_PER_THREAD 10000000
#define NOTIFY_AT 1000000
#define LIMIT 3000
#define OFFSET 4364860500

using namespace std;

struct thread_data{
	int thread_id;
	list<int> start;
};

void stampa(list<int> v);
void *worker(void *threadarg);
void gioca(mano mani[2]);
unsigned long long part(list<int>);
list<int> nth_perm(unsigned long long N);


int main() {
	
	list<int> m[2];
	list<int> mazzo;
	list<int>::iterator it,it2;
	
	//int fig[] = {1,1,2,2,3,3};
	int fig[] = {1,1,1,1,2,2,2,2,3,3,3,3};	
	
	/*mazzo.assign(14,0);
	mazzo.insert(mazzo.end(),fig,fig+6);*/
	
	mazzo.assign(28,0);
	mazzo.insert(mazzo.end(),fig,fig+12);
	
		
	pthread_t threads[NUM_THREADS];
	struct thread_data td[NUM_THREADS];
	int rc,ntd=0,i=0;
	pthread_attr_t attr;
	void *status;

	// Initialize and set thread joinable
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	
	cout << "Giochiamo!" << endl;
	stampa(mazzo);
	cout << endl;
	
	// timing
	struct timeval stop, start;
	gettimeofday(&start, NULL);	
	
	// First hand to play
	unsigned long long startperm = (10*1000+564)*10000000000;
	
	// Create threads and set starting hand for each of them
	for( i=0; i < NUM_THREADS; i++ ){
		cout << "Main: creating thread, " << i << endl;
		// Get nth permutation of starting deck
		list<int> mperm = nth_perm(i*HANDS_PER_THREAD+startperm);
		td[i].thread_id = i;
		td[i].start.assign(mperm.begin(),mperm.end());
		//stampa(td[i].start);
		rc = pthread_create(&threads[i], &attr, worker, (void *)&td[i]);
		if (rc){
			cout << "Error: unable to create thread," << rc << endl;
			exit(-1);
		}
	}
	
	gettimeofday(&stop, NULL);
	printf("Main: took %lu\n", stop.tv_usec - start.tv_usec);
   
	pthread_attr_destroy(&attr);
	
	// Join threads
	for( i=0; i < NUM_THREADS; i++ ){
		rc = pthread_join(threads[i], &status);
		if (rc){
			cout << "Error: unable to join," << rc << endl;
			exit(-1);
		}
		cout << "Main: completed thread id " << i ;
		cout << ", exiting with status " << status << endl;
	}
	
	
	cout << "Main: program exiting." << endl;

	pthread_exit(NULL);
	
	return 0;
}

void stampa(list<int> v) {
	for (list<int>::iterator it = v.begin(); it != v.end(); it++)
		cout << *it << ' ';
	cout << endl;
}

void *worker(void *threadarg) {
	struct thread_data *my_data;
	my_data = (struct thread_data *) threadarg;
	long long int i = 0;
	mano m[2];

	list<int> lmazzo = my_data->start;
	int tid = my_data->thread_id;
	int mazzo[40];
	copy(lmazzo.begin(),lmazzo.end(),mazzo);
	
	cout << "Thread id: " << my_data->thread_id <<". Initial hand: " << mano(mazzo,0,20).to_string() << " - " <<mano(mazzo,20,40).to_string() << endl;
	
	struct timeval stop, start;
	gettimeofday(&start, NULL);
	
	do {
		m[0] = mano(mazzo,0,20);
		m[1] = mano(mazzo,20,40);
		gioca(m);
		if(++i%NOTIFY_AT==0) printf("Thread %d: %d\n",tid,i);
		if(i>HANDS_PER_THREAD) break;
		
	} while ( next_permutation(mazzo,mazzo+40) );
	
	gettimeofday(&stop, NULL);
	printf("Thread %d: took %lu\n", tid, stop.tv_sec - start.tv_sec);

	pthread_exit(NULL);
}

void gioca(mano mani[2]) {
	mano A=mani[0], B=mani[1], tavola;
	int turno = 0;
	int cnt = 0;
	int da_girare = 0;
	int carta;
	int prese = 0;
	int N = LIMIT;
	
	while (prese<N && !mani[0].is_empty() && !mani[1].is_empty()) {
		carta = mani[turno].pop();
		tavola.push(carta);

		
		if (da_girare!=0 && carta==0) {	
			if(--da_girare==0) {
					turno = 1 - turno;
					mani[turno].cat(tavola);
					tavola.empty();
					prese++;					
				}
		} else {			
			da_girare = carta;
			turno = 1 - turno;
		}	
		cnt++;
	}
	
	if (prese==N) {
		cout << "Partita lunga:" << endl;
		cout << A.to_string() << " - " << B.to_string() << endl;
	}
}

int fact(int n) {
	switch (n) {
		case 2:
			return 2;
		case 3:
			return 6;
		case 4: 
			return 24;
		default:
			return 1;
	}
}

unsigned long long part(list<int> inizio) {
	unsigned long long res = 1;
	//int carte[] = {14,2,2,2};
	int carte[] = {28,4,4,4};
	for (list<int>::iterator it = inizio.begin(); it != inizio.end(); it++)
		carte[*it]--;
	
	int somma = carte[0]+carte[1]+carte[2]+carte[3];
	
	for (int i = somma; i>carte[0]; i--) {
		res *= i;
	}
	res /= (fact(carte[1])*fact(carte[2])*fact(carte[3]));
	
	return res;
}

list<int> nth_perm(unsigned long long N) {
	unsigned long long cnt = 0, t = 0, t2;
	list<int> res;
	//int carte[] = {14,2,2,2};
	//int ncarte = 20;
	int carte[] = {28,4,4,4};
	int ncarte = 40;
	
	while (cnt<=N && res.size()!=ncarte) {
		list<int> tmp;
		tmp.assign(res.begin(),res.end());
		int i=0;
		t = 0;
		while (t<=N-cnt && i<4) {
			if(carte[i]) {
				tmp.push_back(i);
				t2 = part(tmp);
				t += t2;
				tmp.pop_back();
			}
			
			i++;
		}
		i--;
		cnt += t-t2;
		res.push_back(i);
		carte[i]--;	
	}
	
	return res;
}

