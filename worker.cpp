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
	int msg; // 0 read, 1 write, 2 terminate
	int address; // memory address
	int isWrite; // 1 for write mode, 0 for read mode
} msgbuffer;

struct simClock
{
	int seconds;
	long long nanoseconds;
};

const int sh_key = ftok("key.val", 26);
const int PAGE_SIZE = 1024;
const int PAGES_PER_PROCESS = 32;
const int MAX_ADDRESS = PAGES_PER_PROCESS * PAGE_SIZE;

long long calculateTime(int lastSec, long long lastNano, int simSec, long long simNano) // gives me the time in nano seconds
{
	return ((long long)(simSec - lastSec) * 1000000000LL) + abs(simNano - lastNano);
}

int main(int argc, char** argv) 
{
		if (argc < 1)
			return EXIT_FAILURE;
		printf("Argv %s worker\n\n", argv[0]);
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

		int lastSec = simClock->seconds;
		long long lastNano = simClock->nanoseconds;
		msgbuffer reply;

		printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %lld\n--Just Starting\n", getpid(), getppid(), simClock->seconds, simClock->nanoseconds);
		bool done = false;
		int memoryAccesses = 0;
		const int TERMINATION = 1000 + (rand() % 201 - 100);

		while (!done)
		{
			srand(getpid() ^ time(NULL));
			
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

			if (memoryAccesses >= TERMINATION)
			{
				if ((rand() % 100) < 5)
				{
					reply.msg = 2;
					reply.mtype = getppid();
					reply.pid = getpid();
					if (msgsnd(msqid, &reply, sizeof(reply) - sizeof(long), 0) == -1)
					{
						perror("Worker:msgsnd failed");
					}

					done = true;
					break;
				}
			}

			if (calculateTime(lastSec, lastNano, simClock->seconds, simClock->nanoseconds) >= 100000000)
			{
				// generate random memory address
				int pageNumber = rand() % PAGES_PER_PROCESS;
				int offset = rand() % PAGE_SIZE;
				int address = (pageNumber * PAGE_SIZE) + offset;
				
				// determine if read or write (read bias)
				int isWrite = (rand() % 100 < 20) ? 1 : 0; // 20% chance of write
				
				reply.msg = isWrite ? 1 : 0;
				reply.address = address;
				reply.isWrite = isWrite;
				reply.mtype = getppid();
				reply.pid = getpid();

				printf("WORKER: Requesting %s of address %d\n", isWrite ? "write" : "read", address);

				if (msgsnd(msqid, &reply, sizeof(reply) - sizeof(long), 0) == -1) 
				{
					perror("Worker: msgsnd failed");
				}
				
				memoryAccesses++;
				lastSec = simClock->seconds;
				lastNano = simClock->nanoseconds;

				if (msgrcv(msqid, &buf, sizeof(msgbuffer), getpid(), 0) == -1)
                        	{
                                	perror("msgrcv in parent failed\n");
                                	exit(1);
                        	}
			}
               	}
		// Just in case
		shmdt(simClock);
		return EXIT_SUCCESS;
}

