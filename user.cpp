#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
int main(int argc, char** argv) {
		printf("Hello from Child.c, a new executable!\n");
		printf("My process id is: %d\n",getpid());
		printf(" I got %d arguments: \n", argc);

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

