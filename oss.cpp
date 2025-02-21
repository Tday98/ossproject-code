#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/wait.h>
#include<map>
#include<string>
#include<sys/ipc.h>
#include<sys/shm.h>

using namespace std;

/*
 * Author: Tristan Day CS 4760
 * Professor: Mark Hauschild
 */

const int sh_key = ftok("worker.cpp", 26);
int shm_id;

struct simClock
{
	int seconds;
	int nanoseconds;
}

struct PCB 
{
	int occupied;
	pid_t pid;
	int startSeconds;
	int startNano;
}

struct PCB processTable[20];
struct simClock *clock;

void incrementClock();

class WorkerLauncher 
{
	private:
		int n_proc;
		int n_simul;
		int n_inter;
	
	public:
		// Constructor to build UserLauncher object
		WorkerLauncher(int n, int s, int t, int i) : n_proc(n), n_simul(s), n_time(t), n_inter(i) {}

		void launchProcesses() 
		{
			int ranProcesses {0};
			while (ranProcesses < n_proc) 
			{
				//manageSimProcesses();
				incrementClock();
				pid_t childPid = fork();
				
				if (childPid < 0)
				{
					perror("Fork failed");
					exit(EXIT_FAILURE);
				} else if (childPid == 0) // Have process lets execute it 
				{
					execl("./user", "user", to_string(n_iter).c_str(), NULL); // execl needs to terminate with NULL pointer

					perror("execl failed");
					exit(EXIT_FAILURE);

				}
			      	processTable[ranProcesses].occupied = 1;
				processTable[ranProcesses].pid = childPid;	
				ranProcesses++;
			}
			waitProcesses();
		}
	private:
		void manageSimProcesses()
		// Function that manages the number of allowed simultaneous processes	
		{
			int status {};
			size_t currentSimul = n_simul;
			while (processTable.size() >= currentSimul)
			{
				pid_t finishedChild = waitpid(-1, &status, WNOHANG); //waitpid() returns the child pid and status when it finishes!
				printf("\nPID: %d has finished with status %d\n", finishedChild, status); 
				// TODO: remove process from processTable!
				currentSimul--;
				if (!currentSimul)
					break;
			}
		}
		
		void waitProcesses()
		// Function that waits for any leftover processes to finish based on whats left in the process table
		{
			int status {};
			while (!processTable.empty())
			{
				pid_t child = waitpid(-1, &status, WNOHANG);
                        	if (!finalChild)
                       		{
                                	printf("PID: %d has finished with status %d\n", finalChild, status);
                                	// TODO: remove process from processTable
                        	}
			}
		}

		void autoShutdown()
		{
			if (clock->seconds >= 60)
			{
				shmdt(clock);
				shmctl(shm_id, IPC_RMID, NULL);
			}
		}
};

void incrementClock()
{
	clock->nanoseconds += 100000000; // Lets start with a hundred million nanoseconds or 100ms
	if (clock->nanoseconds >= 1000000000)
	{
		clock->seconds += 1;
		clock->nanoseconds = 0; // move seconds up nanoseconds back to 0
	}
}

WorkerLauncher argParser(int argc, char** argv)
{
	int opt = {};
        int n_proc, n_simul, n_time n_inter = {};
        while((opt = getopt(argc, argv, "hn:s:t:")) != -1)
        {
                switch(opt)
                {
                        case 'h':
                                printf("You have called the -%c flag.\nTo use this program you need to supply 3 flags:\n"
                                                "-n proc for how many processes you would like to create\n"
                                                "-s simul for how many simultaneous processes you would like\n"
                                                "-t iter for how many iterations you would like\n"
                                                "ex: oss -n 3 -s 3 -t 8\n\n", opt);
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
        printf("Values acquired: -n %d, -s %d, -t %d\n\n", n_proc, n_simul, n_iter);	
	
	return UserLauncher(n_proc, n_simul, n_iter);
}

int main(int argc, char** argv) 
{
	shm_id = shmget(sh_key, sizeof(struct simClock), IPC_CREAT | 0666);
	if (shm_id <= 0)
	{
		fprintf(stderr, "Shared memory get failed\n");
		exit(EXIT_FAILURE);
	}

	clock = (struct simClock *)shmat(shm_id, 0, 0);
	if (clock <= 0)
	{
		fprintf(stderr, "attaching clock to shared memory failed\n");
		exit(EXIT_FAILURE);
	}

	clock->seconds = 0;
	clock->nanoseconds = 0;

	printf("Start clock values: %d seconds %d nanoseconds\n\n", clock->seconds, clock->nanoseconds);

	WorkerLauncher launcher = argParser(argc, argv);
	launcher.launchProcesses();

	// cleanup shared memory
	shmdt(clock);
	shmctl(shm_id, IPC_RMID, NULL);

	return EXIT_SUCCESS;
}

