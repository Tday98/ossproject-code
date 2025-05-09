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
	int msg; // request, release or terminate
	int resourceID; // which resource
	int units; // how many resources
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

struct ResourceDescriptor 
{
	int totalInstances = 10;
	int availableInstances = 10;
	int allocation[20]; // This should basically be a mirror of the PCB table and what I mean is that the indexs should lineup 1 to 1
	int request[20];
};

// Global Values


const int MAX_PROCESSES = 18;
const int NUM_RESOURCES = 5;
simClock* clockPtr;
const int sh_key = ftok("key.val", 26);
int shm_id;
int msqid;
struct PCB processTable[MAX_PROCESSES];
struct ResourceDescriptor resourceTable[NUM_RESOURCES];
struct simClock *simClock;
volatile sig_atomic_t terminateFlag = 0;

std::queue<int> qB;

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

void handleRequest(int pcbIndex, int resourceID, int units) // This function should take in the PCB index so we can find it easily and then also make 
	// the changes in our ResourceTable if the request can be granted
{
	msgbuffer bufResp;
	if (resourceTable[resourceID].availableInstances >= units) // We have enough resources lets grant the request.
	{
		resourceTable[resourceID].availableInstances -= units; // remove resources from table
		resourceTable[resourceID].allocation[pcbIndex] += units; // allocate resources to process

		logwrite("OSS: %d units of Resource Table %d given to process PID %d.\n", units, resourceID, processTable[pcbIndex].pid);
	} else // not enough available resources currently so lets block the process
	{
		resourceTable[resourceID].request[pcbIndex] = units;
		processTable[pcbIndex].blocked = 1;

		logwrite("OSS: PID %d blocked. Not able to fulfill request from Resource Table %d for %d units.\n", processTable[pcbIndex].pid, resourceID, units);
		
		qB.push(pcbIndex); // Push PCB index into blocked queue and need to let process know its blocked.
		bufResp.msg = 5;
	}
	// Reply back to worker letting it know that its request was finally granted
	bufResp.mtype = processTable[pcbIndex].pid;
	bufResp.pid = getpid();
	msgsnd(msqid, &bufResp, sizeof(bufResp) - sizeof(long), 0);
}

void handleRelease(int pcbIndex, int resourceID, int units) // Release units of resources from a process if it sends a message relaying that it has finished utilizing the resources.
{
	if (resourceTable[resourceID].allocation[pcbIndex] >= units) // Only release up to the amount of units that the process has said to release
	{
		resourceTable[resourceID].allocation[pcbIndex] -= units;
		resourceTable[resourceID].availableInstances += units;

		logwrite("OSS: Process PID %d thoughtfully relinqueshed %d units to Resource Table %d.\n", processTable[pcbIndex].pid, units, resourceID);  
	}
	// Reply back to worker letting it know that its request was finally granted
        msgbuffer bufResp;
        bufResp.mtype = processTable[pcbIndex].pid;
        bufResp.pid = getpid();
        bufResp.msg = 2; // chose seven so need to handle that on the worker process side to relay that it has been unblocked.
	msgsnd(msqid, &bufResp, sizeof(bufResp) - sizeof(long), 0);
}

void handleTerminate(int pcbIndex) // Process has terminated either by itself or forcefully through my deadlock detection algorithm.
{
	for (int i = 0; i < NUM_RESOURCES; i++)
	{
		if (resourceTable[i].allocation[pcbIndex] > 0)
		{
			resourceTable[i].availableInstances += resourceTable[i].allocation[pcbIndex];
			resourceTable[i].allocation[pcbIndex] = 0; // add resources back to availability and set the allocation table back to zero
		}
	}
	
	logwrite("OSS: Process PID %d terminated. \n", processTable[pcbIndex].pid);
	kill(processTable[pcbIndex].pid, SIGTERM);
	processTable[pcbIndex].occupied = 0;
	processTable[pcbIndex].blocked = 0;
}

void unblockBlockedQueue()
{
	int size = qB.size(); // iterator constraint
	for (int i = 0; i < size; i++)
	{
		int pcbIndex = qB.front();
		qB.pop();

		if (!processTable[pcbIndex].occupied || !processTable[pcbIndex].blocked) // If we land on a segment that continues no process or no blocked process lets break out.
		{
			continue;
		}

		for (int j = 0; j < NUM_RESOURCES; j++) // need to check every resource and see if it meets our request which was blocked earlier
		{
			int request = resourceTable[j].request[pcbIndex];

			if (request > 0 && resourceTable[j].availableInstances >= request) // If we have the request lets grant it 
			{
				resourceTable[j].availableInstances -= request;
				resourceTable[j].allocation[pcbIndex] += request;
				resourceTable[j].request[pcbIndex] = 0; // We have updated the available and allocated now lets reset the requests

				processTable[pcbIndex].blocked = 0; // No longer blocked!
			
				logwrite("OSS: Process PID %d unblocked and request granted at %d seconds %lld nanoseconds.\n", processTable[pcbIndex].pid, simClock->seconds, simClock->nanoseconds);
				
				// Reply back to worker letting it know that its request was finally granted
				msgbuffer bufResp;

				bufResp.mtype = processTable[pcbIndex].pid;
				bufResp.pid = getpid();
				bufResp.msg = 7; // chose seven so need to handle that on the worker process side to relay that it has been unblocked.
				msgsnd(msqid, &bufResp, sizeof(bufResp) - sizeof(long), 0);

				break;
			}
		}

		if (processTable[pcbIndex].blocked)
			qB.push(pcbIndex);
	}
}

bool req_lt_avail(const int *req, const int *avail, const int pnum, const int num_res)
{
        int i (0);
        for (; i < num_res; i++)
                if (req[pnum*num_res+i] > avail[i] )
                        break;
        return ( i == num_res );
}

bool deadlock(const int *available, const int m, const int n, const int *request, const int *allocated) 
{
	int work[m]; // m resources
	bool finish[n]; // n processes
	for (int i = 0; i < m; work[i] = available[++i]);
	for (int i = 0; i < n; finish[i++] = false);
	int p (0);
	for (; p < n; p++) 
	{
		if ( finish[p] ) continue;
		if (req_lt_avail (request, work, p, m)) {
			finish[p] = true;
			for (int i (0); i < m; i++)
				work[i] += allocated[p*m+i];
			p = -1;
		}
	}
	for (p = 0; p < n; p++)
		if ( !finish[p] )
			break;
	return (p != n);
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

