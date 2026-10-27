#include <iostream>
#include <list>
#include <algorithm>
#include <time.h>

using namespace std;

void stampa(list<int> v) {
	for (list<int>::iterator it = v.begin(); it != v.end(); it++)
		cout << *it << ' ';
	cout << endl;
}

void gioca(list<int> mani[]) {
	list<int> A=mani[0], B=mani[1], tavola, curr;
	int turno = 0;
	int cnt = 0;
	int da_girare = 0;
	int carta;
	int prese = 0;
	int N = 100;
	
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

int main() {

	int A[] = {0,1,0,0,2,3};
	int B[] = {2,0,1,0,3,0};
	list<int> Al (A, A + sizeof(A) / sizeof(int) );
	list<int> Bl (B, B + sizeof(B) / sizeof(int) );
	
	list<int> m[2];
	list<int> mazzo;
	int fig[] = {1,1,2,2,3,3};
	list<int>::iterator it,it2;
	
	mazzo.assign(14,0);
	mazzo.insert(mazzo.end(),fig,fig+6);
	
	stampa(mazzo);
	
	int i = 0;
	
	time_t timer1,timer2;
	time(&timer1);
	

	cout << "Giochiamo!\n";
	do {
		it = mazzo.begin();
		advance(it, 10);
		m[0].assign(mazzo.begin(),it);
		//advance(it, 1);
		m[1].assign(it,mazzo.end());
		gioca(m);
		if(++i%10000==0) cout << i << endl;
		
	} while ( next_permutation(mazzo.begin(),mazzo.end()) );
	
	time(&timer2);
	
	cout << difftime(timer2,timer1);
	
	
	return 0;
}
