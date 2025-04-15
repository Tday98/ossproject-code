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

int q0count = 0, q1count = 0, q2count = 0;
long long totalUsedCPUTime = 0;
long long idleTime = 0;
long long nextSnapshotTime = 500000000;
const int correctionFactor = 1000000000;
const int msCorrect = 1000000;
const int sh_key = ftok("key.val", 26);
int shm_id;
static FILE* logfile = nullptr;
int msqid;
int totalMessages;
int totalProcesses;
int logLineCount = 0;
std::deque<int> q0, q1, q2, qB;

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

struct PCB 
{
	int occupied;
	pid_t pid;
	int startSeconds;
	long long startNano;
	int serviceTimeSeconds;
	int serviceTimeNano;
	int eventWaitSec;
	int eventWaitNano;
	int blocked;
	int priority;
	int messagesSent;
};

struct PCB processTable[20];
struct simulClock *simClock;

//void incrementClock();
void findProcesses(size_t *activeProcesses);
void endProcess(pid_t *child);
void generateWorkTime(int n_inter, int *wseconds, long long *wnanoseconds);
void printPCB();
void PCB_entry(pid_t *child);
bool dispatchProcess();
void interrupt_catch(int sig);
void logwrite(const char *format, ...);
void finalOutput();
void unblock();
void snapshot();

class WorkerLauncher 
{
	private:
		int n_proc;
		const char *f_name;
		chrono::steady_clock::time_point start;
	
	public:
		// Constructor to build UserLauncher object
		WorkerLauncher(int n, const char *f, chrono::steady_clock::time_point start) : n_proc(n), f_name(f), start(start) {}

		void launchProcesses() 
		{
			/*
			* launchProcesses launches processes only if it matches the -i value flag by taking the difference of the current time against the last child process launch time.
			* This assures that processes adhere to the delay value.
			*/
			logfile = fopen(f_name, "w");
			if (!logfile)
			{
				perror("Failed to open log file");
				exit(EXIT_FAILURE);
			}

        		key_t key;
        		system("touch msgq.txt");
        		// get a key for our message queue
        		if ((key = ftok("msgq.txt", 1)) == -1) {
                		perror("ftok");
                		exit(1);
        		}
        		// create our message queue
        		if ((msqid = msgget(key, PERMS | IPC_CREAT)) == -1) {
                		perror("msgget in parent");
                		exit(1);
        		}
        		printf("Message queue set up\n");
			
			int ranProcesses {0};
			size_t currentProcesses{0};
			while (((ranProcesses < n_proc || currentProcesses > 0) && totalProcesses < n_proc) && ranProcesses < 100) 
			{
				ranProcesses++;
				pid_t childPid = fork();
				PCB_entry(&childPid);
				if (childPid < 0)
				{
					perror("Fork failed");
					exit(EXIT_FAILURE);
				} else if (childPid == 0) // Have process lets execute it 
				{
					execl("./worker", "worker", NULL); // execl needs to terminate with NULL pointer
					perror("execl failed");
					exit(EXIT_FAILURE);					
				}

				findProcesses(&currentProcesses);
				totalProcesses++;
				unblock();
				bool dispatched = dispatchProcess();
				if (!dispatched)
				{
					idleTime += 100000;
					simClock->nanoseconds += 100000;
					if (simClock->nanoseconds >= 1000000000) 
					{
 						simClock->seconds += 1;
						simClock->nanoseconds -= 1000000000;
					}
				}
				simClock->nanoseconds += 1000; // simulate OSS overhead
				autoShutdown();	
			}
			while (true)
			{
				unblock();
				bool dispatched = dispatchProcess();
				if (!dispatched)
				{
					idleTime += 100000;
                                        simClock->nanoseconds += 100000;
                                        if (simClock->nanoseconds >= 1000000000)
                                        {
                                                simClock->seconds += 1;
                                                simClock->nanoseconds -= 1000000000;
                                        }
				}
				findProcesses(&currentProcesses);
				if (currentProcesses <= 0) 
				{
					break;
				}
				autoShutdown();
			}
			autoShutdown();
		}
	private:
		
		void autoShutdown()
		// checks against the real time using the chrono library and if longer than 60 seconds of simulated time has gone by close processes and exit.
		{
			auto now = chrono::steady_clock::now();
			auto totalTime = chrono::duration_cast<chrono::seconds>(now - start).count();
			if (totalTime >= 3)
			{
				shmdt(simClock);
				shmctl(shm_id, IPC_RMID, NULL);
				for (int i = 0; i < 20; i++)
				{	
					if (processTable[i].occupied)
					{
						pid_t childPid = processTable[i].pid;
                        			fprintf(stderr, "Killing child PID %d\n", childPid);
                        			kill(childPid, SIGTERM);
					}
				}
				printf("\nTime exceeded cleaning up and shutting down.\n");
				finalOutput();
				fclose(logfile);
				if (msgctl(msqid, IPC_RMID, NULL) == -1)
        			{
                			perror("msgctl failed in interrupt_catch");
                			exit(EXIT_FAILURE);
        			}
				exit(EXIT_SUCCESS);
			}
			int checker {};
			for (int i = 0; i < 20; i++)
			{
				if (processTable[i].occupied)
				{
					checker++;
				}

			}
			if (!checker)
			{
				shmdt(simClock);
				shmctl(shm_id, IPC_RMID, NULL);
				printf("\nAll processes completed, cleaning up and shutting down.\n");
				finalOutput();
				fclose(logfile);
				if (msgctl(msqid, IPC_RMID, NULL) == -1)
        			{
                			perror("msgctl failed in interrupt_catch");
                			exit(EXIT_FAILURE);
        			}
				exit(EXIT_SUCCESS);
			}
		}
};

void finalOutput()
{
	logwrite("\n=== Final Statistics ===\n");
    	logwrite("Total CPU Time Used: %lld ns\n", totalUsedCPUTime);
    	logwrite("Total Idle Time: %lld ns\n", idleTime);
    	logwrite("Queue 0 Dispatched: %d\n", q0count);
    	logwrite("Queue 1 Dispatched: %d\n", q1count);
    	logwrite("Queue 2 Dispatched: %d\n", q2count);

    	long long totalSimTime = ((long long)simClock->seconds * 1000000000LL) + simClock->nanoseconds;
    	float cpuUtil = 100.0f * totalUsedCPUTime / totalSimTime;
    	float idleUtil = 100.0f * idleTime / totalSimTime;

    	logwrite("Simulated Runtime: %lld ns\n", totalSimTime);
    	logwrite("CPU Utilization: %.2f%%\n", cpuUtil);
    	logwrite("Idle Time Ratio: %.2f%%\n", idleUtil);
}

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
			q0.push_back(i); // This sets up our MLFQs
			processTable[i].priority = 0;
			processTable[i].blocked = 0;
			break;
		}
	}
}

void unblock()
{
	for (auto iterator = qB.begin(); iterator != qB.end(); )
	{
		int i = *iterator; //give me the value of the iterator
		if (simClock->seconds > processTable[i].eventWaitSec || (simClock->seconds == processTable[i].eventWaitSec && simClock->nanoseconds >= processTable[i].eventWaitNano))
		{
			processTable[i].blocked = 0;
			processTable[i].priority = 0;
			q0.push_back(i);

			logwrite("OSS: PID %d unblocked. Now in q0 %d seconds %lld nano\n", processTable[i].pid, simClock->seconds, simClock->nanoseconds);

			iterator = qB.erase(iterator); //this will pull us out of the for loop
		} else
		{
			iterator++; //keep moving through for loop
		}
	}
}

void waitProcesses()
{
	int status {};
	pid_t child = waitpid(-1, &status, 0);
	if (child)
	{
		printf("PID: %d has finished with status %d\n", child, status);
		endProcess(&child);	
	}
}

bool dispatchProcess() 
{
	int index = 0;
	int timeQuantum = 0;

	if (!q0.empty()) 
	{
		index = q0.front();
		q0.pop_front();
		timeQuantum = 10000000; // 10ms because its q0
	} else if (!q1.empty()) 
	{
		index = q1.front();
		q1.pop_front();
		timeQuantum = 20000000; // 20ms because its q2
	} else if (!q2.empty()) 
	{
		index = q2.front();
		q2.pop_front();
		timeQuantum = 40000000;
	} else 
	{
		return false;
	}

	pid_t childPid = processTable[index].pid;

	msgbuffer msg;
	msg.mtype = childPid;
	msg.pid = getpid();
	msg.intData = timeQuantum;
	strcpy(msg.strData, "Message from dispatchProcess");

	if (msgsnd(msqid, &msg, sizeof(msgbuffer) - sizeof(long), 0) == -1) 
	{
		perror("OSS: msgsnd dispatchProcess() failed");
		return false;
	}

	logwrite("OSS: Sent %dns time quantum; PID %d; q%d; %d seconds; %lld nanoseconds\n", timeQuantum, childPid, processTable[index].priority, simClock->seconds, simClock->nanoseconds);

	msgbuffer reply;
	if (msgrcv(msqid, &reply, sizeof(msgbuffer) - sizeof(long), getpid(), 0) == -1) 
	{
		perror("OSS: msgrcv failed in dispatchProcess");
		return false;
	}

	int usedTime = abs(reply.intData);
	simClock->nanoseconds += usedTime;
	totalUsedCPUTime += usedTime;
	if (reply.intData == timeQuantum)
	{
		if (processTable[index].priority == 0) q0count++;
		else if (processTable[index].priority == 1) q1count++;
		else q2count++;
	}

	if (simClock->nanoseconds >= 1000000000)
	{
		simClock->seconds += simClock->nanoseconds / 1000000000;
		simClock->nanoseconds %= 1000000000;
	}
	snapshot();

	logwrite("OSS: Received message PID: %d; intData: %d\n", reply.pid, reply.intData);

	if (reply.intData < 0)
	{
		logwrite("OSS: PID %d terminated.\n", reply.mtype);
		waitProcesses();
		return true;
	}

	if (reply.intData == timeQuantum) 
	{
		if (processTable[index].priority == 0)
		{
			processTable[index].priority = 1;
			q1.push_back(index);
		} else if (processTable[index].priority == 1)
		{
			processTable[index].priority = 2;
			q2.push_back(index);
		} else {
			q2.push_back(index);
		}

		logwrite("OSS: PID %d used time quantum. Pushed to q%d.\n", reply.pid, processTable[index].priority);
	} else 
	{
		processTable[index].blocked = 1;

		long long blockTime = simClock->nanoseconds + 100000000;
		processTable[index].eventWaitNano = blockTime % 1000000000;
		processTable[index].eventWaitSec = simClock->seconds + (blockTime / 1000000000);

		qB.push_back(index);
		logwrite("OSS: PID %d put into qB. Process will be unblocked at %d seconds and %lld nanoseconds.\n", reply.pid, processTable[index].eventWaitSec, processTable[index].eventWaitNano);
	}
	return true;
}

void snapshot()
{
	long long currentTime = ((long long)simClock->seconds * 1000000000LL) + simClock->nanoseconds;
	if (currentTime >= nextSnapshotTime && logLineCount < 10000)
	{
		printPCB();
		logwrite("Queue Snapshot at %d;%lld — Q0: %lu Q1: %lu Q2: %lu Blocked: %lu\n", simClock->seconds, simClock->nanoseconds, q0.size(), q1.size(), q2.size(), qB.size());
	}
	nextSnapshotTime += 500000000;
}

void interrupt_catch(int sig)
{
	logwrite("\nOSS: Caught SIGINT, cleaning up processes. SIGNAL:%d\n", sig);

	for (int i = 0; i < 20; i++)
	{
		if (processTable[i].occupied)
		{
			pid_t childPid = processTable[i].pid;
			logwrite("Killing child PID %d\n", childPid);
			kill(childPid, SIGTERM);
		}
	}

	//wait for processes to terminate
	for (int i = 0; i < 20; i++)
	{
		if (processTable[i].occupied) 
		{
			waitpid(processTable[i].pid, NULL, 0);
		}
	}
	shmdt(simClock);
	shmctl(shm_id, IPC_RMID, NULL);
	finalOutput();
	fclose(logfile);
	if (msgctl(msqid, IPC_RMID, NULL) == -1)
	{
		perror("msgctl failed in interrupt_catch");
		exit(EXIT_FAILURE);
	}
	exit(EXIT_FAILURE);
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

void printPCB()
{
	if (simClock->nanoseconds == 0 || simClock->nanoseconds == 500000000)
	{
		printf("\nOSS PID:%d SysClockS: %d SysclockNano: %lld\nProcess Table:\n", getpid(), simClock->seconds, simClock->nanoseconds);
		printf("Entry\tOccupied PID\tStartS\tStartN\n");
		for (int i = 0; i < 20; i++) printf("%d\t%d\t%d\t%d\t%lld\n", i, processTable[i].occupied, processTable[i].pid, processTable[i].startSeconds, processTable[i].startNano);
	}
}

void findProcesses(size_t *activeProcesses)
{
	(*activeProcesses) = 0;
	for (int i = 0; i < 20; i++)
	{
		if (processTable[i].occupied)
		{
			(*activeProcesses)++;
		}
	}
}

void endProcess(pid_t *child)
{
	for (int i = 0; i < 20; i++)
	{
		if (processTable[i].pid == (*child))
		{
			processTable[i].occupied = 0;
		}
	}
}

WorkerLauncher argParser(int argc, char** argv)
{
	chrono::steady_clock::time_point start = chrono::steady_clock::now();
	int opt = {};
        int n_proc = {};
	const char *f_name;
        while((opt = getopt(argc, argv, "hn:f:")) != -1)
        {
                switch(opt)
                {
                        case 'h':
                                printf("You have called the -%c flag.\nTo use this program you need to supply 4 flags:\n"
                                                "-n proc for how many processes you would like to create\n"
						"-f file name to store oss logs to.\n"
                                                "ex: oss -n 3 -f logsfile.txt\n\n", opt);
                                exit(EXIT_SUCCESS);
                        case 'n':
                                n_proc = atoi(optarg);
                                break;
			case 'f':
				f_name = optarg;
				break;
                        case '?':
                                // case ? takes out all the incorrect flags and causes the program to fail. This helps protect the program from undefined behavior
                                fprintf(stderr, "Incorrect flags submitted -%c\n\n", optopt);
                                exit(EXIT_FAILURE);
                }
        }
        printf("Values acquired: -n %d, -f %s\n\n", n_proc, f_name);	
	
	return WorkerLauncher(n_proc, f_name, start);
}

int main(int argc, char** argv) 
{
	//signal for CTRL-C interrupt
	signal(SIGINT, interrupt_catch);

	shm_id = shmget(sh_key, sizeof(struct simulClock), IPC_CREAT | 0666);
	if (shm_id <= 0)
	{
		fprintf(stderr, "Shared memory get failed\n");
		exit(EXIT_FAILURE);
	}

	simClock = (struct simulClock *)shmat(shm_id, 0, 0);
	if (simClock <= (void *)0)
	{
		fprintf(stderr, "attaching clock to shared memory failed\n");
		exit(EXIT_FAILURE);
	}

	simClock->seconds = 0;
	simClock->nanoseconds = 0;

	printf("Start clock values: %d seconds %lld nanoseconds\n\n", simClock->seconds, simClock->nanoseconds);

	WorkerLauncher launcher = argParser(argc, argv);
	launcher.launchProcesses();

	// cleanup shared memory
	shmdt(simClock);
	shmctl(shm_id, IPC_RMID, NULL);

	return EXIT_SUCCESS;
}

