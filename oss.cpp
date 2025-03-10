#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/wait.h>
#include<map>
#include<string>
#include<sys/ipc.h>
#include<sys/shm.h>
#include<random>
#include<chrono>

using namespace std;

/*
 * Author: Tristan Day CS 4760
 * Professor: Mark Hauschild
 */
const int correctionFactor = 1000000000;
const int msCorrect = 1000000;
const int sh_key = ftok("key.val", 26);
int shm_id;

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

struct PCB processTable[20];
struct simulClock *simClock;

void incrementClock();
void findProcesses(size_t *activeProcesses);
void endProcess(pid_t *child);
void generateWorkTime(int n_inter, int *wseconds, long long *wnanoseconds);
void printPCB();
void PCB_entry(pid_t *child);

class WorkerLauncher 
{
	private:
		int n_proc;
		int n_simul;
		int n_time;
		int n_inter;
		chrono::steady_clock::time_point start;
	
	public:
		// Constructor to build UserLauncher object
		WorkerLauncher(int n, int s, int t, int i, chrono::steady_clock::time_point start) : n_proc(n), n_simul(s), n_time(t), n_inter(i), start(start) {}

		void launchProcesses() 
		{
			/*
			* launchProcesses launches processes only if it matches the -i value flag by taking the difference of the current time against the last child process launch time.
			* This assures that processes adhere to the delay value.
			*/
			long long currentTime {0};
			long long lastChildTime {0};
			int lastChildSeconds {0};
			long long lastChildNano {0};
			int ranProcesses {0};
			size_t currentProcesses{0};
			int wseconds {};
			long long wnanoseconds {};
			int waitms = n_inter * msCorrect;
			while (ranProcesses < n_proc) 
			{
				manageSimProcesses();
				incrementClock();
				
				printPCB();
				currentTime = (long long)simClock->seconds * correctionFactor + simClock->nanoseconds;
			       	lastChildTime = lastChildSeconds * correctionFactor + lastChildNano;	
				if (currentTime - lastChildTime >= waitms)
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
				}
				
			}
			size_t activeWorkers {};
			findProcesses(&activeWorkers);
			while (activeWorkers > 0)
			{
				incrementClock();
				printPCB();
				waitProcesses();
				findProcesses(&activeWorkers);
				autoShutdown();
			}
			autoShutdown();
		}
	private:
		void manageSimProcesses()
		// Function that manages the number of allowed simultaneous processes	
		{
			int status {};
			size_t currentSimul = n_simul;
			size_t active = 0;
			findProcesses(&active);
			while (active >= currentSimul)
			{
				incrementClock();
				printPCB();
				pid_t finishedChild = waitpid(-1, &status, WNOHANG); //waitpid() returns the child pid and status when it finishes!
				if (finishedChild)
				{
					printf("\nPID: %d has finished with status %d\n", finishedChild, status); 
					endProcess(&finishedChild);
					
					active = 0;
					findProcesses(&active);
				}
				if (!currentSimul)
					break;
				autoShutdown();
			}
		}
		
		void waitProcesses()
		// Function that waits for any leftover processes to finish based on whats left in the process table
		{
			int status {};
			pid_t child = waitpid(-1, &status, WNOHANG);
                       	if (child)
              		{
                               	printf("PID: %d has finished with status %d\n", child, status);
                               	endProcess(&child);	
                        }
			autoShutdown();
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
				printf("\nTime exceeded, cleaning up and shutting down.\n");
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
				exit(EXIT_SUCCESS);
			}
		}
};

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
	simClock->nanoseconds += 1000; // Lets try 0.001ms 0.0005ms was slightly too slow
	if (simClock->nanoseconds >= 1000000000)
	{
		simClock->seconds += 1;
		simClock->nanoseconds = 0; // move seconds up nanoseconds back to 0
	}
}

WorkerLauncher argParser(int argc, char** argv)
{
	chrono::steady_clock::time_point start = chrono::steady_clock::now();
	int opt = {};
        int n_proc, n_simul, n_time, n_inter = {};
        while((opt = getopt(argc, argv, "hn:s:t:i:")) != -1)
        {
                switch(opt)
                {
                        case 'h':
                                printf("You have called the -%c flag.\nTo use this program you need to supply 4 flags:\n"
                                                "-n proc for how many processes you would like to create\n"
                                                "-s simul for how many simultaneous processes you would like\n"
                                                "-t time for the maximum time you would like your processes to run\n"
						"-i interval in ms to launch children, added delay so children dont spawn super fast\n"
                                                "ex: oss -n 3 -s 3 -t 7 -i 100\n\n", opt);
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
                        case '?':
                                // case ? takes out all the incorrect flags and causes the program to fail. This helps protect the program from undefined behavior
                                fprintf(stderr, "Incorrect flags submitted -%c\n\n", optopt);
                                exit(EXIT_FAILURE);
                }
        }
        printf("Values acquired: -n %d, -s %d, -t %d, -i %d\n\n", n_proc, n_simul, n_time, n_inter);	
	
	return WorkerLauncher(n_proc, n_simul, n_time, n_inter, start);
}

int main(int argc, char** argv) 
{
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

