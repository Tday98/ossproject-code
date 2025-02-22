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

const int sh_key = ftok("key.val", 26);

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

		struct simulClock *simClock = (struct simulClock *)shmat(shm_id, 0, 0);
		if (simClock <= (void *)0)
		{
			fprintf(stderr, "Worker failed shmat");
			exit(EXIT_FAILURE);
		}
		int end_secondtime = simClock->seconds + wseconds;
		int end_nanotime = wnanoseconds;
		printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n--Just Starting\n", getpid(), getppid(), simClock->seconds, simClock->nanoseconds, wseconds, wnanoseconds);
		int time = 1;
		int lastSeconds = simClock->seconds;
		//int lastNanoseconds = simClock->nanoseconds;
		while (simClock->seconds < end_secondtime || (simClock->seconds == end_secondtime && simClock->nanoseconds < end_nanotime))
		{
			if (simClock->seconds > lastSeconds && simClock->seconds <= end_secondtime)
			{
				lastSeconds = simClock->seconds;
		//		lastNanoseconds = simClock->nanoseconds;
				printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %d TermTimeS: %d TermTimeNano: %d\n%d seconds have passed since starting\n", getpid(), getppid(), simClock->seconds, simClock->nanoseconds, wseconds, wnanoseconds, time);
				time++;
			}
			
		}
		shmdt(simClock);
		return EXIT_SUCCESS;
}

