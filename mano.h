#include <stdio.h>
#include <fstream>
#include <string>
#include <sstream>
#include <iostream>

#define SIZE 20

class mano {
	int start, end;
	public:
		int carte[2*SIZE] = {0};
		mano();
		mano(int [], int, int);
		void push(int);
		void cat(mano);
		int pop();
		bool is_empty();	
		void stampa(char *outn);
		int len();
		void empty();
		std::string to_string();
};

int mano::len() {
	if (end > start ) {
		return end-start;
	} else {
		return end-start+2*SIZE;
	}
}

mano::mano() {
	start = 0;
	end = 0;
}

mano::mano(int m[], int in, int fi) {
	std::copy(m+in,m+fi,carte);
	start = 0;
	end = fi-in;
}

int mano::pop() {
	int carta = carte[start];
	start = (start+1)%(2*SIZE);
	return carta;
}

void mano::push(int x) {
	carte[end] = x;
	end = (end+1)%(2*SIZE);
}

void mano::cat(mano stack) {
	int l = stack.len();
	for(int i = 0;i<l;i++) {
		this->push(stack.pop());
	}
}

bool mano::is_empty() {
	return end==start;
}

void mano::stampa(char *out) {
	std::fstream fs;
	fs.open(out, std::fstream::app);

	for(int i=0;i<2*SIZE;i++) 
		fs << carte[i] << " ";
	fs << "\n";
	fs.close();
}

void mano::empty() {
	start = 0;
	end = 0;
}

std::string mano::to_string() {
	std::ostringstream oss("");
	if(start>end) {	
		for(int i = start; i < 2*SIZE; i++) {
			oss << carte[i];
		}
		for(int i = 0; i < end; i++) {
			oss << carte[i];
		}
	} else {
		for(int i = start; i < end; i++) {
			oss << carte[i];
		}
	}
	return oss.str();
}
