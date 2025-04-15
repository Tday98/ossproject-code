#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/msg.h>
#include<cstring>
#include<ctime>

/**
 * Author: Tristan Day CS 4760
 * Professor: Mark Hauschild
 */
#define PERMS 0666
typedef struct msgbuffer {
	long mtype;
	pid_t pid;
	char strData[100];
	int intData;
} msgbuffer;

struct simulClock
{
	int seconds;
	long long nanoseconds;
};

const int sh_key = ftok("key.val", 26);

int main(int argc, char** argv) 
{
		if (argc < 1)
			return EXIT_FAILURE;
		printf ("Argv: %s\n", argv[0]);
		int shm_id = shmget(sh_key, sizeof(struct simulClock), 0666);
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

		struct simulClock *simClock = (struct simulClock *)shmat(shm_id, 0, 0);
		if (simClock <= (void *)0)
		{
			fprintf(stderr, "Worker failed shmat");
			exit(EXIT_FAILURE);
		}
		printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %lld\n--Just Starting\n", getpid(), getppid(), simClock->seconds, simClock->nanoseconds);
		bool done = false;
		while (!done)
		{
			if ( msgrcv(msqid, &buf, sizeof(msgbuffer), getpid(), 0) == -1) {
				perror("msgrcv in parent failed\n");
				exit(1);
			}
			int timeQuantum = buf.intData;

			srand(getpid() ^ time(NULL)); // This allows worker to seed randomness based on pid and time(NULL)
			int outcome = rand() % 100;
			int usedTime = 0;

			if (outcome < 20) { // signal termination
				usedTime = -(rand() % timeQuantum);
			} else if (outcome < 60) { // 40% chance that full quantum used
				usedTime = timeQuantum;
			} else { // 40% chance that we are blocked
				usedTime = rand() % timeQuantum;
			}

			msgbuffer reply;
			reply.mtype = getppid();
			reply.pid = getpid();
			reply.intData = usedTime; // Positive means blocked or used all of it and Negative means that it terminated
			strcpy(reply.strData, "Response from worker process");

			if (msgsnd(msqid, &reply, sizeof(reply) - sizeof(long), 0) == -1) {
				perror("Worker: msgsnd failed");
			}

			if (usedTime < 0) {
				printf("WORKER: PID %d has terminated due to negative usedTime. \n", getpid());
				shmdt(simClock);
				return EXIT_SUCCESS;
			}
               	}
		// Just in case
		shmdt(simClock);
		return EXIT_SUCCESS;
}

