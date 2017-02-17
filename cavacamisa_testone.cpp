#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <sstream>
#include <string>
#include <list>
#include <algorithm>
#include <sys/time.h>
#include <pthread.h>
#include "mano.h"
#include <map>

#define NUM_THREADS 4
#define HANDS_PER_THREAD 10000000
#define NOTIFY_AT 1000000
#define LIMIT 100000000
#define OFFSET 4364860500

using namespace std;

void stampa(list<int> v);
void gioca(mano mani[2]);
unsigned long long part(list<int>);
list<int> nth_perm(unsigned long long N);


int main() {
	
	int mazzo[] = {0,0,3,0,2,0,2,0,3,0,3,1,0,0,0,0,0,3,1,0,0,0,1,0,0,2,0,0,0,0,2,0,0,0,0,0,0,0,1,0};
	
	mano m[2];
	
	struct timeval stop, start;
	gettimeofday(&start, NULL);
	
	
	m[0] = mano(mazzo,0,20);
	m[1] = mano(mazzo,20,40);
	gioca(m);		
	
	gettimeofday(&stop, NULL);
	printf("took %lu\n", stop.tv_sec - start.tv_sec);
	
	
	return 0;
}

void stampa(list<int> v) {
	for (list<int>::iterator it = v.begin(); it != v.end(); it++)
		cout << *it << ' ';
	cout << endl;
}

void gioca(mano mani[2]) {
	mano A=mani[0], B=mani[1], tavola;
	mano m[2];
	int turno = 0;
	int cnt = 0;
	int da_girare = 0;
	int carta;
	int prese = 0;
	int N = LIMIT;
	
	map<string,int> mymap;
	string m1,m2,t,ms;
	
	while (prese<N && !mani[0].is_empty() && !mani[1].is_empty()) {
		m1 = mani[turno].to_string();
		m2 = mani[1-turno].to_string();
		t = tavola.to_string();
		ms = m1+"-"+m2+"-"+t; 
		cout << cnt << ": " << ms << endl;
		if (mymap.count(ms)>0) {
			cout << "Partita infinita!\n";
			cout << "Questo stato era presente alla mossa: " << mymap[ms] << endl;
			cout << "Prese totali: " << prese << endl;
			return;
		} else {
			mymap[ms]=cnt;
		}	
		
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
		cout << "tante" << endl;
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

