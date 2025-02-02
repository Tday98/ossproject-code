#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
int main(int argc, char** argv) {
		int i;
		for (i =0; i < argc; i++) {
			printf("USER PID:%d  PPID:%d Iteration:%d before sleeping\n", getpid(), getppid(), i+1);
			sleep(1);
			printf("USER PID:%d  PPID:%d Iteration:%d after sleeping\n", getpid(), getppid(), i+1);
		}
		printf("\nUser is now ending.\n");

		sleep(3);
		return EXIT_SUCCESS;
}

