cpuusage: cpuusage.c
	cc -Wall -O3 -march=native -mtune=native cpuusage.c -o $@
