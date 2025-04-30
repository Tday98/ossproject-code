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
const int MAX_RESOURCES = 10;
const int NUM_RESOURCES = 5;
int allocation[NUM_RESOURCES] = {0};
int maxPerResource[NUM_RESOURCES] = {MAX_RESOURCES, MAX_RESOURCES, MAX_RESOURCES, MAX_RESOURCES, MAX_RESOURCES};

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
		bool blocked = false;	

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

		int createdSec = simClock->seconds;
		long long createdNano = simClock->nanoseconds;

		int lastSec = simClock->seconds;
		long long lastNano = simClock->nanoseconds;
		msgbuffer reply;

		printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %lld\n--Just Starting\n", getpid(), getppid(), simClock->seconds, simClock->nanoseconds);
		bool done = false;
		while (!done)
		{
			srand(getpid() ^ time(NULL));
			if (msgrcv(msqid, &buf, sizeof(msgbuffer), getpid(), 0) == -1) 
			{
				perror("msgrcv in parent failed\n");
				exit(1);
			}
			
			if (buf.msg == 5) // We're now blocked
				blocked = true;
			while (blocked)
			{
				if (msgrcv(msqid, &buf, sizeof(msgbuffer), getpid(), 0) == -1) 
				{
                                perror("msgrcv in parent failed\n");
                                exit(1);
                        	}
				if (buf.msg == 7)
					blocked = false;
			}

			if (calculateTime(createdSec, createdNano, simClock->seconds, simClock->nanoseconds) >= 1000000000)
			{
				if ((rand() % 100) < 5)
				{
					reply.msg = 2;
					for (int i = 0; i < NUM_RESOURCES; i++)
					{
						if (allocation[i] > 0)
						{
							allocation[i] = 0;
						}
					}
					done = true;
					break;
				}
			}
			if (calculateTime(lastSec, lastNano, simClock->seconds, simClock->nanoseconds) >= 250000000)
			{
				int outcome = rand() % 100;

				if (outcome < 60) // 60% request resources
				{  
					int targetResource = rand() % NUM_RESOURCES;
					if (allocation[targetResource] + 1 <= maxPerResource[targetResource])
					{
						reply.msg = 0; // request from OSS
						reply.resourceID = targetResource;
						reply.units = 1;
						allocation[targetResource] += 1;		
					} else 
					{
						reply.msg = 2;
						reply.resourceID = -1;
						reply.units = 0;
					}
				} else if (outcome < 80) // 20% release a resource
				{	 
					int release = -1;
					int targetResource = rand() % NUM_RESOURCES;
					int i;
					while (allocation[targetResource] == 0 && i < 100)
					{
						targetResource = rand() % NUM_RESOURCES;
						i++;
					}
					if (allocation[targetResource] > 0)
						release = targetResource;

					if (release != -1)
					{
						reply.msg = 1; // signal release to OSS
						reply.resourceID = release;
						reply.units = 1;
						allocation[release] -= 1;
					}
				} else // 20% no request or release 
				{ 
				
				}

				reply.mtype = getppid();
				reply.pid = getpid();

				if (msgsnd(msqid, &reply, sizeof(reply) - sizeof(long), 0) == -1) 
				{
					perror("Worker: msgsnd failed");
				}
				lastSec = simClock->seconds;
				lastNano = simClock->nanoseconds;
			}
               	}
		// Just in case
		shmdt(simClock);
		return EXIT_SUCCESS;
}

