#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/ipc.h>
#include<sys/shm.h>

/**
 * Author: Tristan Day CS 4760
 * Professor: Mark Hauschild
 */

struct simulClock
{
	int seconds;
	int nanoseconds;
};

const int sh_key = ftok("worker.cpp", 26);

int main(int argc, char** argv) 
{
		if (argc < 3)
			return EXIT_FAILURE;
		int wseconds = atoi(argv[1]);
		int wnanoseconds = atoi(argv[2]);

		int shm_id = shmget(sh_key, sizeof(struct simulClock), 0666);
		if (shm_id < 0) 
		{
			fprintf(stderr, "Worker failed shmget\n");
			exit(EXIT_FAILURE);
		}

		struct simulClock *clock = (struct simulClock *)shmat(shm_id, 0, 0);
		if (clock <= (void *)0)
		{
			fprintf(stderr, "Worker failed shmat");
			exit(EXIT_FAILURE);
		}
		int end_secondtime = clock->seconds + wseconds;
		int end_nanotime = clock->nanoseconds + wnanoseconds;

		printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n--Just Starting\n", getpid(), getppid(), clock->seconds, clock->nanoseconds, wseconds, wnanoseconds);
		int timekeeper = clock->seconds;
		int time = 1;
		while (1)
		{
			if (end_secondtime >= clock->seconds && end_nanotime >= clock->nanoseconds)
			{
				if (timekeeper < clock->seconds)
				{
					printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n%d seconds have passed since starting\n", getpid(), getppid(), clock->seconds, clock->nanoseconds, wseconds, wnanoseconds, time);
					timekeeper = clock->seconds;
					time++;
				}
			}
			else
				return EXIT_SUCCESS;
		}

		return EXIT_SUCCESS;
}

