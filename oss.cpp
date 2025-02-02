#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/wait.h>

#include <string>

using namespace std;

int main(int argc, char** argv) {
	int opt = {};
	int n_proc, n_simul, n_iter = {};
	while((opt = getopt(argc, argv, "hn:s:t:")) != -1)
	{
		switch(opt)
		{
			case 'h':
				printf("You have called the -%c flag.\nTo use this program you need to supply 3 flags:\n"
						"-n proc for how many processes you would like to create\n"
						"-s simul for how many simultaneous processes you would like\n"
						"-t iter for how many iterations you would like\n"
						"ex: oss -n 3 -s 3 -t 8\n\n", opt);
				return EXIT_SUCCESS;
			case 'n':
				n_proc = atoi(optarg);
				break;
			case 's':
				n_simul = atoi(optarg);
				break;
			case 't':
				n_iter = atoi(optarg);
				break;
			case '?':
				// case ? takes out all the incorrect flags and causes the program to fail. This helps protect the program from undefined behavior
				fprintf(stderr, "Incorrect flags submitted -%c\n\n", optopt);
				return EXIT_FAILURE;
		}
	}
	printf("Values acquired: -n %d, -s %d, -t %d\n\n", n_proc, n_simul, n_iter);

	return EXIT_SUCCESS;
}

