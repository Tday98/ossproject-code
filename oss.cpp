#include<unistd.h>
#include<sys/types.h>
#include<stdio.h>
#include<stdlib.h>
#include<sys/wait.h>
#include<map>
#include<string>

using namespace std;

/*
 * Author: Tristan Day CS 4760
 * Professor: Mark Hauschild
 */

class UserLauncher 
{
	private:
		int n_proc;
		int n_simul;
		int n_iter;
		map<pid_t, pid_t> processes;
	
	public:
		// Constructor to build UserLauncher object
		UserLauncher(int n, int s, int t) : n_proc(n), n_simul(s), n_iter(t) {}

		void launchProcesses() 
		{
			for (int i = 0; i < n_proc; i++) 
			{
				manageProcesses();
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

				} else
				{
					processes[childPid] = childPid; // else means that we have a child process so lets push that process into a map so we know how many we have and we will have O(1) access.  
				}
			}
			waitProcesses();
		}
	private:
		void manageProcesses()
		// Function that should manage the number of running processes	
		{
			while (processes.size() >= static_cast<size_t>(n_simul))
			{
				pid_t finishedChild = wait(NULL); //wait(NULL) returns the child process when it finishes!
				auto iterator = processes.find(finishedChild);
				if (iterator != processes.end())
				{
					printf("\nProcess Number: %d being erases from process map\n", finishedChild); 
					processes.erase(finishedChild); // If a process has finished pop it off the queue so now we can add another process
				} else 
				{
					printf("Process %d not found", finishedChild);
				}
			}
		}
		
		void waitProcesses()
		{
			while (!processes.empty()) 
			{
				int wstatus;
				pid_t childPid = waitpid(-1, &wstatus, 0);
				processes.erase(childPid);	
			}	
		}
};

UserLauncher argParser(int argc, char** argv)
{
	int opt = {};
        int n_proc, n_simul, n_iter = {};
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
                                n_iter = atoi(optarg);
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
	UserLauncher launcher = argParser(argc, argv);
	launcher.launchProcesses();
	return EXIT_SUCCESS;
}

