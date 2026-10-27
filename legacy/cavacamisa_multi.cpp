#include <iostream>
#include <stdio.h>
#include <sstream>
#include <string>
#include <list>
#include <algorithm>
#include <sys/time.h>
#include <pthread.h>

#define NUM_THREADS 4
#define HANDS_PER_THREAD 10000000
#define NOTIFY_AT 1000000
#define LIMIT 100
#define OFFSET 4364860500

using namespace std;

struct thread_data{
	int thread_id;
	list<int> start;
};

void stampa(list<int> v);
void *worker(void *threadarg);
void gioca(list<int> mani[]);
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
	
	struct timeval stop, start;
	gettimeofday(&start, NULL);	
	
	/*do {
		if(i%HANDS_PER_THREAD==0) {
			cout << "main() : creating thread, " << ntd << endl;
			td[ntd].thread_id = ntd;
			td[ntd].start.assign(mazzo.begin(),mazzo.end());

			rc = pthread_create(&threads[ntd], &attr, worker, (void *)&td[ntd]);
			ntd++;
			if (rc){
				cout << "Error:unable to create thread," << rc << endl;
				exit(-1);
			}
		}
		i++;		
	} while ( next_permutation(mazzo.begin(),mazzo.end()) );*/
	
	for( i=0; i < NUM_THREADS; i++ ){
		cout << "Main: creating thread, " << i << endl;
		list<int> mperm = nth_perm(i*HANDS_PER_THREAD+OFFSET);
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
	
	for( i=0; i < NUM_THREADS; i++ ){
		rc = pthread_join(threads[i], &status);
		if (rc){
			cout << "Error: unable to join," << rc << endl;
			exit(-1);
		}
		cout << "Main: completed thread id :" << i ;
		cout << "  exiting with status :" << status << endl;
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
	int i = 0;
	list<int> m[2];
	list<int>::iterator it;

	list<int> mazzo = my_data->start;
	int tid = my_data->thread_id;
	
	stringstream ss;
	for (list<int>::iterator it = mazzo.begin(); it != mazzo.end(); it++)
		ss << *it;
		
	string s = ss.str();
	printf("Thread id: %d - Initial hand: %s\n",my_data->thread_id,s.c_str());
	
	struct timeval stop, start;
	gettimeofday(&start, NULL);
	
	do {
		it = mazzo.begin();
		//advance(it, 10);
		advance(it, 20);
		m[0].assign(mazzo.begin(),it);
		m[1].assign(it,mazzo.end());
		gioca(m);
		if(++i%NOTIFY_AT==0) printf("Thread %d: %d\n",tid,i);
		if(i>HANDS_PER_THREAD) break;
		
	} while ( next_permutation(mazzo.begin(),mazzo.end()) );
	
	gettimeofday(&stop, NULL);
	printf("Thread %d: took %lu\n", tid, stop.tv_sec - start.tv_sec);

	pthread_exit(NULL);
}

void gioca(list<int> mani[]) {
	list<int> A=mani[0], B=mani[1], tavola, curr;
	int turno = 0;
	int cnt = 0;
	int da_girare = 0;
	int carta;
	int prese = 0;
	int N = LIMIT;
	
	while (prese<N && mani[0].size()!=0 && mani[1].size()!=0) {
		carta = mani[turno].front();
		tavola.push_back(carta);
		mani[turno].pop_front();
		
		if (da_girare!=0 && carta==0) {	
			if(--da_girare==0) {
					turno = 1 - turno;
					mani[turno].splice(mani[turno].end(),tavola);
					prese++;					
				}
		} else {			
			da_girare = carta;
			turno = 1 - turno;
		}	
		cnt++;
		
		/*cout << cnt << " round:"<< endl;
		stampa(mani[turno]);
		stampa(mani[1-turno]);
		stampa(tavola);
		cout << endl;*/
	}
	
	if (prese==N) {
		stampa(A);
		stampa(B);
		cout << endl;
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

