#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<sys/msg.h>
#include<cstring>

/**
 * Author: Tristan Day CS 4760
 * Professor: Mark Hauschild
 */
#define PERMS 0666
typedef struct msgbuffer {
	long mtype;
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
		if (argc < 3)
			return EXIT_FAILURE;
		int wseconds = atoi(argv[1]);
		long long  wnanoseconds = atoi(argv[2]);

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
		printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %lld TermTimeS: %d TermTimeNano: %lld\n--Just Starting\n", getpid(), getppid(), simClock->seconds, simClock->nanoseconds, wseconds, wnanoseconds);
		long long term_seconds = simClock->seconds + wseconds;
		long long term_nanoseconds = simClock->nanoseconds + wnanoseconds;
		if (term_nanoseconds >= 1000000000)
		{
			term_seconds += 1;
			term_nanoseconds = term_nanoseconds % 1000000000; // should do a rollover correction for nanoseconds...
		}
		printf("%lld term seconds; %lld term nanoseconds\n\n", term_seconds, term_nanoseconds);
		bool done = false;
		int i = 0;
		//long long lastWorkerMessageTime = simClock->nanoseconds;
		while (!done)
		{
			//printf("\n\n in worker while loop !done\n\n");
			if ( msgrcv(msqid, &buf, sizeof(msgbuffer), getpid(), 0) == -1) {
				perror("msgrcv in parent failed\n");
				exit(1);
			}
				i++;
				printf("WORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %lld TermTimeS: %d TermTimeNano: %lld\n--%d iteration has passed since it started \n", getpid(), getppid(), simClock->seconds, simClock->nanoseconds, wseconds, wnanoseconds, i);
				if ((term_seconds < simClock->seconds) || (term_seconds == simClock->seconds && term_nanoseconds <= simClock->nanoseconds))
				{	
					done = true;
				}
				if (done)
				{
					buf.mtype = getppid();
					buf.intData = 2;
					strcpy(buf.strData,"Worker process finished\n");
					printf("\n\nWORKER %d: IN DONE STATEMENT\n\n", getpid());
					if (msgsnd(msqid,&buf,sizeof(msgbuffer)-sizeof(long),0) == -1) {
						perror("msgsnd to parent failed\n");
						exit(1);
					}
					printf("\n\nWORKER PID:%d PPID:%d SysClockS: %d SysclockNano: %lld TermTimeS: %d TermTimeNano: %lld\n--Terminating after sending message back to oss after %d iterations\n\n", getpid(), getppid(), simClock->seconds, simClock->nanoseconds, wseconds, wnanoseconds, i);
					shmdt(simClock);
					return EXIT_SUCCESS;
				} else
				{
					buf.mtype = getppid();
                                        buf.intData = 1;
                                        strcpy(buf.strData,"Worker process still working\n");
					printf("\n\nWORKER %d: IN NOT DONE STATEMENT\n\n", getpid());
                                        if (msgsnd(msqid,&buf,sizeof(msgbuffer)-sizeof(long),0) == -1) {
                                                perror("msgsnd to parent failed\n");
                                                exit(1);
                                        }
				}
		}
		shmdt(simClock);
		return EXIT_SUCCESS;
}

