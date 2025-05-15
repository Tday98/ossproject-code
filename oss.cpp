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
#include<queue>

using namespace std;

/*
 * Author: Tristan Day CS 4760
 * Professor: Mark Hauschild
 */

#define PERMS 0666

// Struct definitions

typedef struct msgbuffer 
{
	long mtype; // parent PID
	pid_t pid; // child PID
	int msg; // 0 read, 1 write or 2 terminate
	int address; // which location in memory
	int mode; // writing or reading 1 for write 0 for read
} msgbuffer;

struct simClock
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
	int blocked;
};

struct PageTable 
{
	int frameNumber; // which frame
	int dirtybit; // something from the specs
	int lastSeconds; // last access time second
	long long lastNano; // last access time nanoseconds
};

struct FrameTable
{
	int occupied; // 1 if occupied
	int dirtybit; // same thing as page table will figure out later
	int lastSeconds;
	long long lastNano;
	pid_t pid; // process id for that frame
	int pageNumber; // page number also discussed in specs
};

// Global Values
const int PAGE_SIZE = 1024; // 1KB page size
const int MEMORY_SIZE = 128 * 1024; // 128KB total memory
const int NUM_FRAMES = MEMORY_SIZE / PAGE_SIZE; // total number of frames
const int PAGES_PER_PROCESS = 32; // 32KB per process as in specs
const int MAX_PROCESSES = 18;
const int NUM_RESOURCES = 5;
const int SEC_TO_NANO = 1000000000LL;
simClock* clockPtr;
const int sh_key = ftok("key.val", 26);
int shm_id;
int msqid;
struct PCB processTable[MAX_PROCESSES];
struct ResourceDescriptor resourceTable[NUM_RESOURCES];
struct simClock *simClock;
struct PageTable processPageTables[MAX_PROCESSES];
struct FrameTableEntry frameTable[NUM_FRAMES];
volatile sig_atomic_t terminateFlag = 0;

std::queue<int> qB;
std::queue<int> qPF;

static FILE* logfile = nullptr;

// Functions for maintaining PCBs and resources

void PCB_entry(pid_t *child)
{
	for (int i = 0; i < MAX_PROCESSES; i++)
	{
		if (!processTable[i].occupied)
		{
			processTable[i].occupied = 1;
			processTable[i].pid = (*child);
			processTable[i].startSeconds = clockPtr->seconds;
			processTable[i].startNano = clockPtr->nanoseconds;
			break;
		}
	}
}

int activeProcesses()
{
	int active = 0;
	for (int i = 0; i < MAX_PROCESSES; i++)
	{
		if (processTable[i].occupied)
		{
			active++;
		}
	}
	return active;
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
	va_end(args);
}
void printPCB()
{
	//if (clockPtr->nanoseconds == 0 || clockPtr->nanoseconds == 500000000)
	//{
		logwrite("\nOSS PID:%d SysClockS: %d SysclockNano: %lld\nProcess Table:\n", getpid(), clockPtr->seconds, clockPtr->nanoseconds);
		logwrite("Entry\tOccupied PID\tStartS\tStartN\tBlocked\n");
		for (int i = 0; i < MAX_PROCESSES; i++) logwrite("%d\t%d\t%d\t%d\t%lld\t%d\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano, processTable[i].blocked);
	//}
}

void printResourceTable()
{
	logwrite("\n\tR0\tR1\tR2\tR3\tR4\n");
	for (int i = 0; i < MAX_PROCESSES; i++) logwrite("P%d\t%d\t %d\t %d\t %d\t %d\n",i, resourceTable[0].allocation[i], resourceTable[1].allocation[i], resourceTable[2].allocation[i], resourceTable[3].allocation[i], resourceTable[4].allocation[i]);
}

// Interrupt function

void interrupt_catch(int sig)
{
        logwrite("\nOSS: Caught SIGINT, cleaning up processes. SIGNAL:%d\n", sig);
	printPCB();
        for (int i = 0; i < MAX_PROCESSES; i++)
        {
                if (processTable[i].occupied)
                {
                        pid_t childPid = processTable[i].pid;
                        logwrite("Killing child PID %d\n", childPid);
                        kill(childPid, SIGTERM);
                }
        }

        //wait for processes to terminate
        for (int i = 0; i < MAX_PROCESSES; i++)
        {
                if (processTable[i].occupied)
                {
                        waitpid(processTable[i].pid, NULL, 0);
                }
        }
        fclose(logfile);
	shmdt(clockPtr);
        shmctl(shm_id, IPC_RMID, nullptr);
        msgctl(msqid, IPC_RMID, nullptr);
        exit(EXIT_FAILURE);
}

int findPCBIndex(int pid)
{
        for (int i = 0; i < MAX_PROCESSES; i++)
        {
                if (processTable[i].occupied && processTable[i].pid == pid)
                {
                        return i;
                }
        }
        return -1;
}

int findFrame() // looks to find an available frame in frame table.
{
	for (int i = 0; i < NUM_FRAMES; i++)
	{
		if (!frameTable[i].occupied)
		{
			return i;
		}
	}
	return -1;
}

int findLRUFrame() // LRU algorithm implementation 
{
	int LRUframe = 0;
	long long LRUtime = (long long)frameTable[0].lastSeconds * SEC_TO_NANO + frameTable[0].lastNano; // grab first time stamp then look for longest time stamp

	for (int i = 1; i < NUM_FRAMES; i++) // loop through frame table and determine which frame was the LRU
	{
		long long loopFrameTime = (long long)frameTable[i].lastSeconds * SEC_TO_NANO + frameTable[i].lastNano;
		if (loopFrameTime < LRUtime) 
		{
			LRUtime = loopFrameTime; // so because time starts from 0 like a stop watch for our time stamps the smaller the number the least recently it was used had to wrap my mind around that
			LRUframe = i;
		}
	}
	return LRUframe;
}

void handlePageFault(int pcbIndex, int pageNumber, int isWrite) // If there are no frames available in our page table we have faulted. isWrite is from the message determines if we are writing or reading
{
	int frame = findFrame();

	if (frame == -1) // no free frames womp womp lets do LRU algorithm
	{
		frame = findLRUFrame();
		int oldPID = frameTable[frame].pid;
		int oldPageNumber = frameTable[frame].pageNumber;

		processPageTables[oldPID][oldPageNumber].frameNumber = -1; // set this frame to available
		
		if (frameTable[frame].dirtybit) // write so add write time
		{
			incrementClock(); // add extra time
			logwrite("OSS: Dirty bit of frame %d set, adding additional time to the clock\n", frame);
		}
	}

	// set new frame in frame table
	frameTable[frame].occupied = 1;
	frameTable[frame].dirtybit = isWrite;
	frameTable[frame].lastSeconds = clockPtr->seconds;
	frameTable[frame].lastNano = clockPtr->nanoseconds;
	frameTable[frame].pid = processTable[pcbIndex].pid
	frameTable[frame].pageNumber = pageNumber;
	
	// set new page in page table
	processPageTables[pcbIndex][pageNumber].frameNumber = frame;
	processPageTables[pcbIndex][pageNumber].dirtybit = isWrite;
	processPageTables[pcbIndex][pageNumber].lastSeconds = clockPtr->seconds;
	processPageTables[pcbIndex][pageNumber].lastNano = clockPtr->nanoseconds;

	logwrite("OSS: Clearing frame %d and swapping in p%d page %d\n", frame, pcbIndex, pageNumber);
}

void printMemoryTables()
{
	logwrite("\nCurrent memory layout at time %d:%lld is:\n", clockPtr->seconds, clockPtr->nanoseconds);
	logwrite("         \tOccupied\tDirtybit\tLastSec\tLastNano\n");

	for (int i = 0; i < NUM_FRAMES; i++) 
	{
		logwrite("Frame %d: %s\t%d\t%d%lld\n", i, 
				frameTable[i].occupied ? "Yes" : "No", 
				frameTable[i].dirtybit, 
				frameTable[i].lastSeconds, 
				frameTable[i].lastNano);
	}

	logwrite("\nProcess Page Table:\n");
	for (int i = 0; i < MAX_PROCESSES; i++) 
	{
        if (processTable[i].occupied) 
	{
            logwrite("P%d page table: [", i);
            for (int j = 0; j < PAGES_PER_PROCESS; j++) 
	    {
                logwrite("%d ", processPageTables[i][j].frameNumber);
            }
            logwrite("]\n");
        }
    }
}

int main(int argc, char* argv[]) 
{
	auto realTime = chrono::steady_clock::now();
    	signal(SIGINT, interrupt_catch);
	int n_proc = 0;
	int n_simul = 0;
	int n_inter = 0;
    	string logfileName = "log.txt";

    	// argument parser
    	int opt;
    	while ((opt = getopt(argc, argv, "hn:s:i:f:")) != -1) 
	{
        	switch (opt) 
		{
            	case 'h':
                	cout << "Usage: ./oss -n x -f filename\n";
                	exit(EXIT_SUCCESS);
            	case 'n':
                	n_proc = atoi(optarg);
                	break;
		case 's':
			n_simul = atoi(optarg);
			break;
		case 'i':
			n_inter = atoi(optarg);
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
    	logfile = fopen(logfileName.c_str(), "w");
    	if (!logfile) 
	{
        	cerr << "Failed to open log file." << endl;
        	exit(EXIT_FAILURE);
    	}
	system("touch msgq.txt");
    	// attach to shared memory with worker
    	key_t key = ftok("msgq.txt", 1);
    	shm_id = shmget(sh_key, sizeof(struct simClock), IPC_CREAT | 0666);
    	clockPtr = (struct simClock *)shmat(shm_id, nullptr, 0);
	if (clockPtr <= (void *)0)
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
		incrementClock();
		
		long long currentSimTime = (long long)clockPtr->seconds * 1000000000LL + clockPtr->nanoseconds;
		if ((currentSimTime - lastFork) >= (n_inter * 1000000) 
				&& totalLaunched <= n_proc
				&& activeProcesses() < n_simul) //check that we can fork again n_inter time restraint and that we havent created more processes than requested 
		{
			pid_t childPid = fork();
			if (childPid < 0)
			{					
				perror("Fork failed");
				exit(EXIT_FAILURE);
			} else if (childPid == 0) // Have process lets execute it 
			{
				execl("./worker", "worker", NULL); // execl needs to terminate with NULL pointer
				perror("execl failed");
				exit(EXIT_FAILURE);					
			} else
			{
				PCB_entry(&childPid);
				logwrite("OSS: Forked worker PID %d at %d:%lld\n", childPid, clockPtr->seconds, clockPtr->nanoseconds);
				totalLaunched++;
				lastFork = currentSimTime;
			}
		}
		msgbuffer buf;
		if (msgrcv(msqid, &buf, sizeof(buf) - sizeof(long), getpid(), IPC_NOWAIT) > 0) 
		{
    
    			int pcbIndex = findPCBIndex(buf.pid);
			
			if (pcbIndex == -1) 
			{
				perror("Something failed in finding the pcbIndex");
				exit(EXIT_FAILURE);
			}

    			if (buf.msg == 0) 
			{
        			handleRequest(pcbIndex, buf.resourceID, buf.units);
    			} else if (buf.msg == 1) 
			{
        			handleRelease(pcbIndex, buf.resourceID, buf.units);
    			} else if (buf.msg == 2) 
			{
       	 			handleTerminate(pcbIndex);
    			}
		}
		unblockBlockedQueue(); // Check to see if any blocked processes now have resources available.	
		if (currentSimTime % 2500000000LL < 10000)
			printResourceTable();
		if (currentSimTime % 5000000000LL < 10000)
			printPCB();
		int request[NUM_RESOURCES * MAX_PROCESSES] = {0};
		int allocated[NUM_RESOURCES * MAX_PROCESSES] = {0};
		int available[NUM_RESOURCES];

		for (int i = 0; i < NUM_RESOURCES; i++)
		{
			available[i] = resourceTable[i].availableInstances;
			for (int j = 0; j < MAX_PROCESSES; j++)
			{
				request[j * NUM_RESOURCES + i] = resourceTable[i].request[j]; // The fancy math steps through the flattened array
				allocated[j * NUM_RESOURCES + i] = resourceTable[i].allocation[j];
			}
		}
		if (currentSimTime % 1000000000LL < 10000)
		{
			if (deadlock(available, NUM_RESOURCES, MAX_PROCESSES, request, allocated))
			{
				logwrite("\nOSS: Deadlock found time: %d seconds %lld nanoseconds\n", clockPtr->seconds, clockPtr->nanoseconds);
				for (int i = 0; i < MAX_PROCESSES; i++) // find blocked processes and terminate it to end deadlock
				{
					if (processTable[i].occupied && processTable[i].blocked)
					{
						logwrite("OSS: PID %d terminated to stop a deadlock\n", processTable[i].pid);
						handleTerminate(i);
						break;
					}
				}
			}
		}
		auto now = chrono::steady_clock::now();
		if (chrono::duration_cast<chrono::seconds>(now - realTime).count() >= 5)
		{
			logwrite("\n\nOSS: 5 second time limit exceeding ending program\n\n");
			terminateFlag = 1;
			break;		
		}
	}

    	// cleaing up shared memory
    	shmdt(clockPtr);
    	shmctl(shm_id, IPC_RMID, nullptr);
    	msgctl(msqid, IPC_RMID, nullptr);
    	fclose(logfile);

    	return EXIT_SUCCESS;
}

