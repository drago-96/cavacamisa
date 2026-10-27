#!/usr/bin/python
import mcavacamisa
import random
import time
import itertools
import numpy
import multiprocessing


def perm_it(start,N=float("inf")):
	it = start
	i = 0
	yield (it,0)
	k = True
	while k:
		k=next_permutation(it)
		i+=1
		if(i>N): break
		yield (it,i)
		
def nth(iterable, n, default=None):
	"Returns the nth item or a default value"
	return next(itertools.islice(iterable, n, None), default)

def next_permutation(a):
    """Generate the lexicographically next permutation inplace.

    https://en.wikipedia.org/wiki/Permutation#Generation_in_lexicographic_order
    Return false if there is no next permutation.
    """
    # Find the largest index i such that a[i] < a[i + 1]. If no such
    # index exists, the permutation is the last permutation
    for i in reversed(range(len(a) - 1)):
        if a[i] < a[i + 1]:
            break  # found
    else:  # no break: not found
        return False  # no next permutation

    # Find the largest index j greater than i such that a[i] < a[j]
    j = next(j for j in reversed(range(i + 1, len(a))) if a[i] < a[j])

    # Swap the value of a[i] with that of a[j]
    a[i], a[j] = a[j], a[i]

    # Reverse sequence from a[i + 1] up to and including the final element a[n]
    a[i + 1:] = reversed(a[i + 1:])
    return True
    
    
    
def work(start,i,N):
	mano = start
	nomep = "Processo "+str(i)
	print nomep, "- Starting from ",mano
	mcavacamisa.play((mano[0:10],mano[10:20]),nomep,fast=False)
	
	for m in perm_it(start,N):
		i=m[1]
		if (i%10000==0): print i
		mano = m[0]
		mcavacamisa.play((mano[0:10],mano[10:20]),nomep)
	



N=800000
procs = 4
	
mazzo = [0]*14
mazzo.extend([1,1,2,2,3,3])

#mani=[[0,0,0,0,0,0,3,0,1,0,0,1,0,0,3,0,2,0,2,0]]

mazzobig=[0]*19
mazzobig.append(1)
mazzobig.extend([0]*9)
mazzobig.extend([1,1,1,2,2,2,2,3,3,3,3])
#mazzobig = [0,2,0,0,0,1,0,0,0,3,0,0,0,3,0,0,0,2,0,3,0,0,2,0,0,3,0,0,1,0,2,1,0,0,0,0,1,0,0,0]

print mazzo

#mazzi = [list(nth(perm_it(mazzo),N*i)) for i in range(procs)]
	
start = time.time()	

mazzi = []
i = 0
for m in perm_it(mazzo):
	i=m[1]
	if (i%N==0): 
		mano = list(m[0])
		mazzi.append(mano)

	
print mazzi

end = time.time()
print(end - start)


   
start = time.time()

jobs = []
for i in range(0, procs):
	out_list = list()
	process = multiprocessing.Process(target=work, args=(mazzi[i],i,N))
	jobs.append(process)

# Start the processes (i.e. calculate the random number lists)		
for j in jobs:
	j.start()

# Ensure all of the processes have finished
for j in jobs:
	j.join()

end = time.time()
print(end - start)
