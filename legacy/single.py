#!/usr/bin/python
import mcavacamisa
import random
import time
import sys
import getopt
import re


myarg = sys.argv[1:]
verbose=False
fast=False
inputmano="";

try:
	opts, args = getopt.getopt(myarg,"m:fv",["mano="])
except getopt.GetoptError:
	print 'single.py -m <deck>'
	sys.exit(2)

for opt, arg in opts:
      if opt in ("-m", "--mano"):
         inputmano = arg
      elif opt == "-f":
         fast = True
      elif opt == "-v":
         verbose = True
         
while(inputmano==""):
	inputmano=raw_input("Inserisci la mano: ")
	pattern = r'[^0-3]'
	if re.search(pattern, inputmano):
		print "Mano non valida. Sono validi solo i numeri da 0 a 3"
		inputmano=""

mano = [int(i) for i in list(inputmano)]

print "Hai inserito",mano

N=len(mano)

start=time.time()

#mano=[0,0,0,0,0,0,3,0,1,0,0,1,0,0,3,0,2,0,2,0]

mcavacamisa.play((mano[0:N/2],mano[N/2:N]),"Main",verbose=verbose,fast=fast)

end = time.time()
print(end - start)
