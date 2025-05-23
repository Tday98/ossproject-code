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
	int isWrite; // writing or reading 1 for write 0 for read
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

// building struct to store memory statistics as per project specs.
struct MemoryStats
{
	long long totalAccesses;
	long long totalPageFaults;
	int lastPrint;
	int seconds;
} memoryStats = {0,0,0,0};

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
struct simClock *simClock;

struct PageTable processPageTables[MAX_PROCESSES][PAGES_PER_PROCESS];
struct FrameTable frameTable[NUM_FRAMES];
volatile sig_atomic_t terminateFlag = 0;

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

void handleTerminate(int pcbIndex) // Process has terminated either by itself or forcefully through my deadlock detection algorithm.
{
	for (int i = 0; i < PAGES_PER_PROCESS; i++) // free all frames used by pcbIndex 
	{
		int frameNumber = processPageTables[pcbIndex][i].frameNumber;
        	if (frameNumber != -1) 
		{
			if (frameTable[frameNumber].dirtybit) 
			{
                	incrementClock(); // add time for writing dirty page
                	logwrite("OSS: Dirty bit of frame %d set, adding additional time to the clock during cleanup\n", frameNumber);
            		}
            
            		// clear the frame
            		frameTable[frameNumber].occupied = 0;
            		frameTable[frameNumber].dirtybit = 0;
            		frameTable[frameNumber].pid = 0;
            		frameTable[frameNumber].pageNumber = 0;
            		frameTable[frameNumber].lastSeconds = 0;
            		frameTable[frameNumber].lastNano = 0;
            
            		// clear the page table entry
            		processPageTables[pcbIndex][i].frameNumber = -1;
            		processPageTables[pcbIndex][i].dirtybit = 0;
            		processPageTables[pcbIndex][i].lastSeconds = 0;
            		processPageTables[pcbIndex][i].lastNano = 0;
        	}
    	}
    	// log termination with memory statistics
    	logwrite("OSS: Process PID %d terminated at time %d:%lld\n", 
			processTable[pcbIndex].pid, 
             		clockPtr->seconds, 
             		clockPtr->nanoseconds);
    
    	// send termination signal to process
    	kill(processTable[pcbIndex].pid, SIGTERM);
    
    	// clear PCB entry
    	processTable[pcbIndex].occupied = 0;
    	processTable[pcbIndex].blocked = 0;
    	processTable[pcbIndex].pid = 0;
    	processTable[pcbIndex].startSeconds = 0;
    	processTable[pcbIndex].startNano = 0;
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
	frameTable[frame].pid = processTable[pcbIndex].pid;
	frameTable[frame].pageNumber = pageNumber;
	
	// set new page in page table
	processPageTables[pcbIndex][pageNumber].frameNumber = frame;
	processPageTables[pcbIndex][pageNumber].dirtybit = isWrite;
	processPageTables[pcbIndex][pageNumber].lastSeconds = clockPtr->seconds;
	processPageTables[pcbIndex][pageNumber].lastNano = clockPtr->nanoseconds;

	logwrite("OSS: Clearing frame %d and swapping in p%d page %d\n", frame, pcbIndex, pageNumber);
}

void printMemoryTables() // print function to display what is requested from the specs
{
	logwrite("\nCurrent memory layout at time %d:%lld is:\n", clockPtr->seconds, clockPtr->nanoseconds);
	logwrite("Frame\tOccupied\tDirtybit\tLastSec\t\tLastNano\n");

	for (int i = 0; i < NUM_FRAMES; i++) 
	{
		logwrite("%d: \t%s\t\t%d\t\t%d\t\t%lld\n", 
				i, 
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

void updateMemoryStats(int isPageFault) {
    	memoryStats.totalAccesses++;

    	if (isPageFault) 
	{
        	memoryStats.totalPageFaults++;
    	}

    	// Print statistics every second
    	if (clockPtr->seconds > memoryStats.lastPrint) 
	{
        	logwrite("\nMemory Statistics at time %d:%lld:\n",
                	clockPtr->seconds, clockPtr->nanoseconds);
        	logwrite("Total memory accesses: %lld\n", memoryStats.totalAccesses);
        	logwrite("Total page faults: %lld\n", memoryStats.totalPageFaults);
        	if (memoryStats.totalAccesses > 0) 
		{
            		logwrite("Overall page fault rate: %.2f%%\n",
                    		(float)memoryStats.totalPageFaults * 100 / memoryStats.totalAccesses);
        	}
    	}
	memoryStats.lastPrint = clockPtr->seconds; // keep track of the last time we printed so we print every second
}

void handleMemoryRequest(int pcbIndex, int address, int isWrite) // handle memory addresses checks for page fault or hit
{
	int pageNumber = address / PAGE_SIZE;
	
	logwrite("OSS: P%d requesting %s of address %d at time %d:%lld\n",
			pcbIndex,
			isWrite ? "write" : "read",
			address,
			clockPtr->seconds,
			clockPtr->nanoseconds);

	if (processPageTables[pcbIndex][pageNumber].frameNumber == -1) // -1 means that we have a page fault (page not in table)
	{
		logwrite("OSS: Address %d is not in a frame, pagefault\n", address);
		handlePageFault(pcbIndex, pageNumber, isWrite);
		processTable[pcbIndex].blocked = 1;
		qPF.push(pcbIndex);
		updateMemoryStats(1); // register page fault in stats table


		msgbuffer buf;
		buf.mtype = processTable[pcbIndex].pid;
		buf.pid = getpid();
		buf.msg = 5;
		msgsnd(msqid, &buf, sizeof(buf) - sizeof(long), 0);
	} else // not == -1 so we have a page hit
	{
		int frame = processPageTables[pcbIndex][pageNumber].frameNumber;
		frameTable[frame].lastSeconds = clockPtr->seconds;
		frameTable[frame].lastNano = clockPtr->nanoseconds;
		frameTable[frame].dirtybit |= isWrite; // dirtybit or isWrite need to evaluate to see if we are still in write mode

		processPageTables[pcbIndex][pageNumber].lastSeconds = clockPtr->seconds;
		processPageTables[pcbIndex][pageNumber].lastNano = clockPtr->nanoseconds;
		processPageTables[pcbIndex][pageNumber].dirtybit |= isWrite;

		logwrite("OSS: Address %d in frame %d, %s data to P%d at time %d:%lld\n",
                 address, 
		 frame, 
		 isWrite ? "writing" : "giving",
                 pcbIndex, 
		 clockPtr->seconds, 
		 clockPtr->nanoseconds);
		updateMemoryStats(0);
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

    	// setup processTable
   	memset(processTable, 0, sizeof(processTable));
    	// launch and receive section
	long long lastFork = 0;
	int totalLaunched = 0;

	for (int i = 0; i < MAX_PROCESSES; i++) // intialize page table nad frame tables
	{
		for (int j = 0; j < PAGES_PER_PROCESS; j++)
		{
			processPageTables[i][j].frameNumber = -1;
			processPageTables[i][j].dirtybit = 0;
		}
	}

	for (int i = 0; i < NUM_FRAMES; i++)
	{
		frameTable[i].occupied = 0;
		frameTable[i].dirtybit = 0;
	}

	while (!terminateFlag) 
	{
		incrementClock();
		
		long long currentSimTime = (long long)clockPtr->seconds * 1000000000LL + clockPtr->nanoseconds;
		
		if (!qPF.empty())
                {
                        int pcbIndex = qPF.front();
                        if (processTable[pcbIndex].occupied)
                        {
                                if ((currentSimTime - processTable[pcbIndex].startNano) >= 14000000) // I/O delay
                                {
                                        processTable[pcbIndex].blocked = 0;
                                        qPF.pop();

                                        msgbuffer buf;
                                        buf.mtype = processTable[pcbIndex].pid;
                                        buf.pid = getpid();
                                        buf.msg = 7; // unblock the process
                                        msgsnd(msqid, &buf, sizeof(buf) - sizeof(long), 0);

                                        logwrite("OSS: Unblocking process %d after page fault at time %d:%lld\n",
                                                        processTable[pcbIndex].pid, clockPtr->seconds, clockPtr->nanoseconds);
                                } else
                                {
                                        qPF.pop();
                                }
                        }
                }
		
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

    			if (buf.msg == 0 || buf.msg == 1) 
			{
        			handleMemoryRequest(pcbIndex, buf.address, buf.isWrite);
    			} else if (buf.msg == 2) 
			{
       	 			handleTerminate(pcbIndex);
    			}
		}
		if (currentSimTime % 100000000LL < 10000)
			printMemoryTables();
		if (currentSimTime % 1500000000LL < 10000)
			printPCB();
		
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

