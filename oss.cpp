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

const int correctionFactor = 1000000000;
const int msCorrect = 1000000;
const int sh_key = ftok("key.val", 26);
int shm_id;
static FILE* logfile = nullptr;
int msqid;
int totalMessages;
int totalProcesses;
std::deque<int> q0, q1, q2;

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
	int priority
};

struct PCB processTable[20];
struct simulClock *simClock;

void incrementClock();
void findProcesses(size_t *activeProcesses);
void endProcess(pid_t *child);
void generateWorkTime(int n_inter, int *wseconds, long long *wnanoseconds);
void printPCB();
void PCB_entry(pid_t *child);
void dispatchProcess();
void interrupt_catch(int sig);
void logwrite(const char *format, ...);
void finalOutput();

class WorkerLauncher 
{
	private:
		int n_proc;
		int n_simul;
		int n_time;
		int n_inter;
		const char *f_name;
		chrono::steady_clock::time_point start;
	
	public:
		// Constructor to build UserLauncher object
		WorkerLauncher(int n, int s, int t, int i, const char *f, chrono::steady_clock::time_point start) : n_proc(n), n_simul(s), n_time(t), n_inter(i), f_name(f), start(start) {}

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

			msgbuffer buf0, buf1;
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

			long long currentTime {0};
			long long lastChildTime {0};
			int lastChildSeconds {0};
			long long lastChildNano {0};
			int ranProcesses {0};
			size_t currentProcesses{0};
			int wseconds {};
			long long wnanoseconds {};
			int waitms = n_inter * msCorrect;
			//long long lastMessageTime = 0;
			while (ranProcesses < n_proc || currentProcesses > 0) 
			{
				incrementClock();

				currentTime = (long long)simClock->seconds * correctionFactor + simClock->nanoseconds;
			       	lastChildTime = lastChildSeconds * correctionFactor + lastChildNano;	
				manageSimProcesses(&buf0, &buf1);
				if (currentTime - lastChildTime >= waitms && ranProcesses < n_proc)
				{
					pid_t childPid = fork();
					PCB_entry(&childPid);		
					if (childPid < 0)
					{
						perror("Fork failed");
						exit(EXIT_FAILURE);
					} else if (childPid == 0) // Have process lets execute it 
					{
						generateWorkTime(n_time, &wseconds, &wnanoseconds);
						execl("./worker", "worker", to_string(wseconds).c_str(), to_string(wnanoseconds).c_str(), NULL); // execl needs to terminate with NULL pointer
						perror("execl failed");
						exit(EXIT_FAILURE);
					
					}
					lastChildSeconds = simClock->seconds;
					lastChildNano = simClock->nanoseconds;

					findProcesses(&currentProcesses);
					ranProcesses++;
					totalProcesses++;
				}
				for (int i = 0; i < 20; i++)
				{
					if (processTable[i].occupied)
					{
						pid_t childProcessID = processTable[i].pid;

						buf0.mtype = childProcessID;
						strcpy(buf0.strData, "OSS -> Worker do next iteration");
						buf0.intData = 1;
						logwrite("OSS: Sending message to worker %d PID %d at time %d;%lld\n", i, childProcessID, simClock->seconds, simClock->nanoseconds);
						if (msgsnd(msqid, &buf0, sizeof(msgbuffer) - sizeof(long), 0) == -1)
						{
							perror("msgsnd in parent");
							exit(1);
						}
						processTable[i].messagesSent += 1;
						totalMessages++;
					if (msgrcv(msqid, &buf1, sizeof(msgbuffer), getpid(), 0) == -1)
                                                {
                                                        perror("msgrcv in parent");
                                                        exit(1);
                                                }
                                                logwrite("OSS: Receiving message from worker PID %d at time %d;%lld\n", buf0.mtype, simClock->seconds, simClock->nanoseconds);
                                                if (buf1.intData == 2)
                                                {
                                                        logwrite("OSS: Message for process complete triggered\n");
                                                        waitProcesses();
                                                }
					}
				}
				autoShutdown();	
			}
			autoShutdown();
		}
	private:
		void manageSimProcesses(msgbuffer *buf0, msgbuffer *buf1)
		// Function that manages the number of allowed simultaneous processes	
		{
			size_t currentSimul = n_simul;
			size_t active = 0;
			findProcesses(&active);
			while (active >= currentSimul)
			{
				incrementClock();
                                for (int i = 0; i < 20; i++)
                                {
                                        if (processTable[i].occupied)
                                        {
                                                pid_t childProcessID = processTable[i].pid;

                                                buf0->mtype = childProcessID;
                                                strcpy(buf0->strData, "OSS -> Worker do next iteration");
                                                buf0->intData = 1;
                                                logwrite("OSS: Sending message to worker %d PID %d at time %d;%lld\n", i, childProcessID, simClock->seconds, simClock->nanoseconds);
                                                if (msgsnd(msqid, buf0, sizeof(msgbuffer) - sizeof(long), 0) == -1)
                                                {
                                                        perror("msgsnd in parent");
                                                        exit(1);
                                                }
                                                processTable[i].messagesSent += 1;
                                        if (msgrcv(msqid, buf1, sizeof(msgbuffer), getpid(), 0) == -1)
                                                {
                                                        perror("msgrcv in parent");
                                                        exit(1);
                                                }
                                                logwrite("OSS: Receiving message from worker PID %d at time %d;%lld\n", buf0->mtype, simClock->seconds, simClock->nanoseconds);
                                                if (buf1->intData == 2)
                                                {
                                                        logwrite("OSS: Message for process complete triggered\n");
                                                        waitProcesses();
                                                }
                                        }
                                }
				findProcesses(&active);
				if (active == 0)
					currentSimul = 0;
				if (!currentSimul)
					return;
				autoShutdown();
			}
		}
		
		void waitProcesses()
		// Function that waits for any leftover processes to finish based on whats left in the process table
		{
			int status {};
			pid_t child = waitpid(-1, &status, 0);
                       	if (child)
              		{
                               	printf("PID: %d has finished with status %d\n", child, status);
                               	endProcess(&child);	
                        }
			//autoShutdown();
		}

		void autoShutdown()
		// checks against the real time using the chrono library and if longer than 60 seconds of simulated time has gone by close processes and exit.
		{
			auto now = chrono::steady_clock::now();
			auto totalTime = chrono::duration_cast<chrono::seconds>(now - start).count();
			if (totalTime >= 60)
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
	logwrite("\n\nFinal total sent OSS messages: %d ; Final total Processes: %d\n\n", totalMessages, totalProcesses);
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

void dispatchProcess() {
	int index = 0;
	int timeQuantum = 0;

	if (!q0.empty()) {
		index = q0.front();
		q0.pop_front();
		timeQuantum = 10000000; // 10ms because its q0
	} else if (!q1.empty()) {
		index = q1.front();
		q1.pop_front();
		timeQuantum = 20000000; // 20ms because its q2
	} else if (!q2.empty()) {
		index = q2.front();
		q2.pop_front();
		timeQuantum = 40000000;
	} else {
		return;
	}

	pid_t childPid = processTable[index].pid;

	msgbuffer msg;
	msg.mtype = childPid;
	msg.intData = timeQuantum;
	msg.pid = childPid;
	strcpy(msg.strData, "Message from dispatchProcess");

	if (msgsend(msqid, &msg, sizeof(msgbuffer) - sizeof(long), 0) == -1) {
		perror("OSS: msgsnd dispatchProcess() failed");
		return;
	}

	logwrite("OSS: Sent %dns time quantum; PID %d; q%d; %d seconds; %lld nanoseconds\n", quantum, childPid, processTable[index].priority, simClock->seconds, simClock->nanoseconds);

	msgbuffer reply;
	if (msgrcv(msqid, %reply, sizeof(msgbuffer) - sizeof(long), getpid(), 0) == -1) {
		perror("OSS: msgrcv failed in dispatchProcess");
		return;
	}

	logwrite("OSS: Received message PID: %d; intData: %d\n", reply.pid, reply.intData);
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

void generateWorkTime(int n_time, int *wseconds, long long *wnanoseconds)
{
	int secmin = 1;
	int secmax = n_time;
	int nanomin = 0;
	long long nanomax = 1000000000;

	random_device rd;
	mt19937 gen(rd());
	uniform_int_distribution<> distribsec(secmin, secmax);
	uniform_int_distribution<> distribnano(nanomin, nanomax);
	*wseconds = distribsec(gen);
	*wnanoseconds = distribnano(gen);
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

void incrementClock()
{
	size_t activeProcesses = {};
	findProcesses(&activeProcesses);
	if (activeProcesses > 0) 
	{
		simClock->nanoseconds += (250000000 / activeProcesses); // 250ms as per instructions divided by amount of processes in PCB
	} else 
	{
		simClock->nanoseconds += 250000000;
	}
	if (simClock->nanoseconds >= 1000000000)
	{
		simClock->seconds += 1;
		simClock->nanoseconds = 0; // move seconds up nanoseconds back to 0
	}
	if (simClock-> nanoseconds % 500000000 == 0)
	{
		printPCB();
	}
}

WorkerLauncher argParser(int argc, char** argv)
{
	chrono::steady_clock::time_point start = chrono::steady_clock::now();
	int opt = {};
        int n_proc, n_simul, n_time, n_inter = {};
	const char *f_name;
        while((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1)
        {
                switch(opt)
                {
                        case 'h':
                                printf("You have called the -%c flag.\nTo use this program you need to supply 4 flags:\n"
                                                "-n proc for how many processes you would like to create\n"
                                                "-s simul for how many simultaneous processes you would like\n"
                                                "-t time for the maximum time you would like your processes to run\n"
						"-i interval in ms to launch children, added delay so children dont spawn super fast\n"
						"-f file name to store oss logs to.\n"
                                                "ex: oss -n 3 -s 3 -t 7 -i 100 -f logsfile.txt\n\n", opt);
                                exit(EXIT_SUCCESS);
                        case 'n':
                                n_proc = atoi(optarg);
                                break;
                        case 's':
                                n_simul = atoi(optarg);
                                break;
                        case 't':
                                n_time = atoi(optarg);
                                break;
			case 'i':
				n_inter = atoi(optarg);
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
        printf("Values acquired: -n %d, -s %d, -t %d, -i %d -f %s\n\n", n_proc, n_simul, n_time, n_inter, f_name);	
	
	return WorkerLauncher(n_proc, n_simul, n_time, n_inter, f_name, start);
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

