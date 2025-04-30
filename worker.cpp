#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/msg.h>
#include<cstring>
#include<ctime>
#include<cstdlib>

/**
 * Author: Tristan Day CS 4760
 * Professor: Mark Hauschild
 */
#define PERMS 0666
typedef struct msgbuffer 
{
	long mtype;
	pid_t pid;
	int msg; // 0 request, 1 release, 2 terminate
	int resourceID;
	int units;
} msgbuffer;

struct simClock
{
	int seconds;
	long long nanoseconds;
};

const int sh_key = ftok("key.val", 26);
const int NUM_RESOURCES = 5;
int allocation[NUM_RESOURCES] = {0};
int requestPerResource[NUM_RESOURCES];

long long calculateTime(int lastSec, long long lastNano, int simSec, long long simNano) // gives me the time in nano seconds
{
	return ((long long)(simSec - lastSec) * 1000000000LL) + abs(simNano - lastNano);
}

int main(int argc, char** argv) 
{
		if (argc < 1)
			return EXIT_FAILURE;
		printf ("Argv: %s\n", argv[0]);
		int shm_id = shmget(sh_key, sizeof(struct simClock), 0666);
		msgbuffer buf;
		buf.mtype = 1;
		int msqid = 0;
		key_t key;
		

		// get a key for our message queue
		if ((key = ftok("msgq.txt", 1)) == -1) {
			perror("ftok");
			exit(1);
		}

		// create our message queue
		if ((msqid = msgget(key, PERMS)) == -1) {
			perror("msgget in child");
			exit(1);
		}

		printf("Child %d has access to the queue\n",getpid());
		
		if (shm_id < 0) 
		{
			fprintf(stderr, "Worker failed shmget\n");
			exit(EXIT_FAILURE);
		}

		struct simClock *simClock = (struct simClock *)shmat(shm_id, 0, 0);
		if (simClock <= (void *)0)
		{
			fprintf(stderr, "Worker failed shmat");
			exit(EXIT_FAILURE);
		}

		int prevSec = simClock->seconds;
		long long prevNano = simClock->nanoseconds;

		printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %lld\n--Just Starting\n", getpid(), getppid(), simClock->seconds, simClock->nanoseconds);
		bool done = false;
		while (!done)
		{
			if (msgrcv(msqid, &buf, sizeof(msgbuffer), getpid(), 0) == -1) {
				perror("msgrcv in parent failed\n");
				exit(1);
			}
			
			srand(getpid() ^ time(NULL)); // This allows worker to seed randomness based on pid and time(NULL)
			int outcome = rand() % 100;
			

			for (int i = 0; i < NUM_RESOURCES; i++)
			{
				requestPerResource[i] = rand() % 2; // request 0 or 1 resource randomly across the 5 resource options
			}

			if (outcome < 60) // 60% request resources
			{  
			
			} else if (outcome < 80) // 20% release a resource
			{ 
			
			} else // 20% no request or release 
			{ 
			
			}

			msgbuffer reply;
			reply.mtype = getppid();
			reply.pid = getpid();

			if (msgsnd(msqid, &reply, sizeof(reply) - sizeof(long), 0) == -1) 
			{
				perror("Worker: msgsnd failed");
			}
               	}
		// Just in case
		shmdt(simClock);
		return EXIT_SUCCESS;
}

