CC ?= clang
CFLAGS ?= -O3 -march=native -Wall -Wextra

all: cvc_enum cvc_check

cvc_enum: cvc_enum.c cvc_engine.h cvc_enum_common.h
	$(CC) $(CFLAGS) -o $@ cvc_enum.c -lpthread

cvc_check: cvc_check.c cvc_engine.h
	$(CC) $(CFLAGS) -o $@ cvc_check.c

# Cross-check the fast engine against reference.py on random deals.
test: cvc_check
	python3 reference.py --random 20000 --seed 5 | cut -d' ' -f1 > .test_deals.txt
	python3 reference.py < .test_deals.txt | sed 's/ mu_steps.*//; s/ loop steps=[0-9]* captures=[0-9]*/ loop/' > .test_ref.txt
	./cvc_check < .test_deals.txt > .test_fast.txt
	cmp .test_ref.txt .test_fast.txt && echo "engine matches reference on 20000 deals"
	rm -f .test_deals.txt .test_ref.txt .test_fast.txt

clean:
	rm -f cvc_enum cvc_check

.PHONY: all test clean
