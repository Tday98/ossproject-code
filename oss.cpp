#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/wait.h>
#include<string>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<random>
#include<chrono>
#include<sys/msg.h>
#include<cstring>
#include<signal.h>
#include<iostream>
#include<fstream>
#include<cstdarg>
#include<cstdio>
#include<deque>

using namespace std;

/*
 * Author: Tristan Day CS 4760
 * Professor: Mark Hauschild
 */

#define PERMS 0666

// Struct definitions

typedef struct msgbuffer 
{
	long mtype;
	pid_t pid;
	int msg;
	int resourceID;
	int units;
} msgbuffer;

struct simulClock
{
	int seconds;
	long long nanoseconds;
};

struct PCB 
{
	int occupied;
	pid_t pid;
	int startSeconds;
	long long startNano;
};

struct ResourceDescriptor 
{
	int total;
	int available;
	int allocation[20];
	int request[20];
};

// Global Values

const int MAX_PROCESSES = 20;
const int NUM_RESOURCES = 5;
simClock* clockPtr;
int shm_id;
int msqid;
PCB processTable[MAX_PROCESSES];
ResourceDescriptor resourceTable[NUM_RESOURCES];
volatile sig_atomic_t terminateFlag = 0;

static FILE* logfile = nullptr;
 
void interrupt_catch(int sig) 
{
    	terminateFlag = 1;
}

// Functions

void PCB_entry(pid_t *child)
{
	for (int i = 0; i < 20; i++)
	{
		if (!processTable[i].occupied)
		{
			processTable[i].occupied = 1;
			processTable[i].pid = (*child);
			processTable[i].startSeconds = simClock->seconds;
			processTable[i].startNano = simClock->nanoseconds;
			break;
		}
	}
}

void incrementClock() 
{
	clockPtr->nanoseconds += 10000; // 10,000 ns per loop iteration
    	if (clockPtr->nanoseconds >= 1000000000) 
	{
        	clockPtr->seconds++;
        	clockPtr->nanoseconds -= 1000000000;
    	}
}

void logwrite(const char* format, ...)
{
	// Found a variadic function that send output to both screen and log file
	va_list args;
	va_start(args, format);
	
	// console print
	vprintf(format, args);
	
	// need separate args for log file.
	va_end(args);
	va_start(args, format);

	if (logfile)
	{
		vfprintf(logfile, format, args);
		fflush(logfile); // flush out so it writes immediately
	}
	logLineCount++;
	va_end(args);
}

int main(int argc, char* argv[]) 
{
    	signal(SIGINT, interrupt_catch);
	int n_proc = 5;
    	string logfileName = "log.txt";

    	// argument parser
    	int opt;
    	while ((opt = getopt(argc, argv, "hn:f:")) != -1) 
	{
        	switch (opt) 
		{
            	case 'h':
                	cout << "Usage: ./oss -n x -f filename\n";
                	exit(EXIT_SUCCESS);
            	case 'n':
                	n_proc = atoi(optarg);
                	break;
            	case 'f':
                	logfileName = optarg;
                	break;
        	case '?':
			// case ? takes out all the incorrect flags and causes the program to fail. This helps protect the program from undefined behavior
			fprintf(stderr, "Incorrect flags submitted -%c\n\n", optopt);
      			exit(EXIT_FAILURE);
		}
    	}

    	// open log file and so that we can write to it
    	logfile.open(logfileName);
    	if (!logfile.is_open()) 
	{
        	cerr << "Failed to open log file." << endl;
        	exit(EXIT_FAILURE);
    	}

    	// attach to shared memory with worker
    	key_t key = ftok("oss.cpp", 42);
    	shm_id = shmget(key, sizeof(simClock), IPC_CREAT | 0666);
    	clockPtr = (simClock*)shmat(shm_id, nullptr, 0);
	if (simClock <= (void *)0)
	{
		fprintf(stderr, "attaching clock to shared memory failed\n");
		exit(EXIT_FAILURE);
	}
    	clockPtr->seconds = 0;
    	clockPtr->nanoseconds = 0;

    	// message queue
    	msqid = msgget(key, IPC_CREAT | 0666);

    	// initialize resources for use
    	for (int i = 0; i < NUM_RESOURCES; i++) 
	{
        	resourceTable[i].totalInstances = 10;
        	resourceTable[i].availableInstances = 10;
        	memset(resourceTable[i].allocation, 0, sizeof(resourceTable[i].allocation));
        	memset(resourceTable[i].request, 0, sizeof(resourceTable[i].request));
    	}

    	// setup processTable
   	 memset(processTable, 0, sizeof(processTable));

    	// launch and receive section
	long long lastFork = 0;
	int totalLaunched = 0;

	while (!terminateFlag) 
	{
		
	}

    	// cleaing up shared memory
    	shmdt(clockPtr);
    	shmctl(shm_id, IPC_RMID, nullptr);
    	msgctl(msqid, IPC_RMID, nullptr);
    	logfile.close();

    	return EXIT_SUCCESS;
}

