#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>

/**
 * Author: Tristan Day CS 4760
 * Professor: Mark Hauschild
 */

int main(int argc, char** argv) 
{
		int i;
		int iteration = atoi(argv[1]);
		
		if (argc < 2) 
			return EXIT_FAILURE;
		
		for (i =0; i < iteration; i++) 
		{
			printf("USER PID:%d  PPID:%d Iteration:%d before sleeping\n", getpid(), getppid(), i+1);
			sleep(1);
			printf("USER PID:%d  PPID:%d Iteration:%d after sleeping\n", getpid(), getppid(), i+1);
		}

		printf("\nUser is now ending.\n");
		
		sleep(2);
		
		return EXIT_SUCCESS;
}

