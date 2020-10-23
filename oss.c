#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>

#include "master.h"

//maximum time to run
#define MAX_RUNTIME 20
//maximum children to create
#define MAX_CHILDREN 100

//Our program options
static unsigned int arg_c = 5;
static char * arg_l = NULL;
static unsigned int arg_t = MAX_RUNTIME;

static pid_t childpids[MAX_CHILDREN];  //array for user pids
static unsigned int C = 0;
static int shmid = -1, msgid = -1;    //shared memory and msg queue ids
static unsigned int interrupted = 0;

static FILE * output = NULL;
static struct shared * shmp = NULL; //pointer to shared memory

//Called when we receive a signal
static void sign_handler(const int sig)
{
  interrupted = 1;
	fprintf(output, "[%i:%i] Signal %i received\n", shmp->sec, shmp->ns, sig);
}

//Create a child process
static pid_t master_fork(const char *prog)
{
	const pid_t pid = fork();  //create process
	if(pid < 0){
		perror("fork");
		return -1;

	}else if(pid == 0){
    //run the specified program
		execl(prog, prog, NULL);
		perror("execl");
		exit(1);

	}else{
    //save child pid
		childpids[C++] = pid;
	}
	return pid;
}

//Wait for all processes to exit
static void master_waitall()
{
  int i;
  for(i=0; i < C; ++i){ //for each process
    if(childpids[i] == 0){  //if pid is zero, process doesn't exist
      continue;
    }

    int status;
    if(waitpid(childpids[i], &status, WNOHANG) > 0){

      if (WIFEXITED(status)) {  //if process exited

        fprintf(output,"Master: Child %u terminated at system time %i at %i:%i\n",
          childpids[i], WEXITSTATUS(status), shmp->sec, shmp->ns);

      }else if(WIFSIGNALED(status)){  //if process was signalled
        fprintf(output,"Master: Child %u killed with signal %d at system time at %i:%i\n",
          childpids[i], WTERMSIG(status), shmp->sec, shmp->ns);
      }
      childpids[i] = 0;
    }
  }
}

//Called at end to cleanup all resources and exit
static void master_exit(const int ret)
{
  //tell all users to terminate
  int i;
  for(i=0; i < C; i++){
    if(childpids[i] <= 0){
      continue;
    }
  	kill(childpids[i], SIGTERM);
  }
  master_waitall();

  if(shmp){
    shmdt(shmp);
    shmctl(shmid, IPC_RMID, NULL);
  }

  if(msgid > 0){
    msgctl(msgid, IPC_RMID, NULL);
  }

  fclose(output);
	exit(ret);
}

//Move time forward
static int update_timer(struct shared *shmp)
{

  shmp->ns += 100;
	if(shmp->ns > 1000000000){ //nanosecond in 1 second
		shmp->sec++;
    if(shmp->sec > 2){
      fprintf(output, "Master: Reached 2 seconds of virtual time\n");
      return -1;
    }
		shmp->ns = 0;
	}
  usleep(10);
  fprintf(output, "Master: Incremented system time with 100 ns\n");
  return 0;
}

//Process program options
static int update_options(const int argc, char * const argv[])
{

  int opt;
	while((opt=getopt(argc, argv, "hc:l:t:")) != -1){
		switch(opt){
			case 'h':
				fprintf(output,"Usage: master [-h]\n");
        fprintf(output,"Usage: master [-n x] [-s x] [-t time] infile\n");
				fprintf(output," -h Describe program options\n");
				fprintf(output," -c x Total of child processes (Default is 5)\n");
        fprintf(output," -l filename Log filename (Default is log.txt)\n");
        fprintf(output," -t x Maximum runtime (Default is 20)\n");
				return 1;

      case 'c':
        arg_c	= atoi(optarg); //convert value -n from string to int
        break;

      case 't':
        arg_t	= atoi(optarg);
        break;

      case 'l':
				arg_l = strdup(optarg);
				break;

			default:
				fprintf(output, "Error: Invalid option '%c'\n", opt);
				return -1;
		}
	}

	if(arg_l == NULL){
		arg_l = strdup("log.txt");
	}
  return 0;
}

//Initialize the shared memory
static int shared_initialize()
{
  key_t key = ftok(FTOK_SHM_PATH, FTOK_SHM_KEY);  //get a key for the shared memory
	if(key == -1){
		perror("ftok");
		return -1;
	}

  const long shared_size = sizeof(struct shared);

	shmid = shmget(key, shared_size, IPC_CREAT | IPC_EXCL | S_IRWXU);
	if(shmid == -1){
		perror("shmget");
		return -1;
	}

  shmp = (struct shared*) shmat(shmid, NULL, 0); //attach it
  if(shmp == NULL){
		perror("shmat");
		return -1;
	}

	key = ftok(FTOK_Q_PATH, FTOK_Q_KEY);
	if(key == -1){
		perror("ftok");
		return -1;
	}

	msgid = msgget(key, IPC_CREAT | IPC_EXCL | 0666);
	if(msgid == -1){
		perror("msgget");
		return -1;
	}
  return 0;
}

//Initialize the master process
static int master_initialize()
{

  if(shared_initialize() < 0){
    return -1;
  }

  //clear timer and pid
  bzero(childpids, sizeof(pid_t)*MAX_CHILDREN);
  shmp->sec	= 0;
	shmp->ns	= 0;

  return 0;
}

static int get_msg(const int msgid, struct msgbuf *m, int msg_type){

	if(msgrcv(msgid, (void*)m, MSG_SIZE, msg_type, 0) == -1){
		perror("msgrcv");
		return EXIT_FAILURE;
	}

	struct msgbuf reply;
	reply.mtype = m->from;	//send back to sender
	reply.from  = getpid();	//put our pid in message

	if(msgsnd(msgid, &reply, MSG_SIZE, 0) == -1){
		perror("msgsnd");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

int main(const int argc, char * const argv[])
{

  if(update_options(argc, argv) < 0){
    master_exit(1);
  }

  output = fopen(arg_l, "w");
  if(output == NULL){
    perror("fopen");
    return 1;
  }

  //signal(SIGCHLD, master_waitall);
  signal(SIGTERM, sign_handler);
  signal(SIGALRM, sign_handler);
  alarm(arg_t);

  if(master_initialize() < 0){
    master_exit(1);
  }

  int i;
  for(i=0; i < arg_c; i++){
    const pid_t pid = master_fork("./user");
    fprintf(output,"Master: Creating new child pid %i at system clock time %i.%i\n", pid, shmp->sec, shmp->ns);
  }

  struct msgbuf msg;	//buffer for message passing
	static int msg_type = CRIT_ANY;

  //run until interrupted
  while(!interrupted){

    //wait for a message
		if(get_msg(msgid, &msg, msg_type) == EXIT_FAILURE)
			return EXIT_FAILURE;

		//check message type and take some action
		switch(msg.mtype){
			case CRIT_LOCK:	//if a user enters the critical section
				msg_type = CRIT_UNLOCK;		//read next message only from him
				break;

			case CRIT_UNLOCK:
				msg_type = CRIT_ANY;		   //read messages from all
				break;

			case CRIT_TERM:
        msg_type = CRIT_ANY;		   //read messages from all

				//output message to the log
				fprintf(output, "Master: Child %i is terminating at system clock time %i.%i\n",
								shmp->shmPID, shmp->sec, shmp->ns);
        shmp->shmPID = 0;
        master_waitall();  //clean if there are any zombies

				if(C < MAX_CHILDREN){
          const pid_t pid = master_fork("./user");
          fprintf(output,"Master: Creating new child pid %i at system clock time %i.%i\n", pid, shmp->sec, shmp->ns);
				}else{  //we have generated all of the children
          interrupted = 1;  //stop master loop
        }
				break;
			default:
				break;
		}

    if(update_timer(shmp) < 0){
      break;
    }
	}

  fprintf(output,"[%i:%i] Master exit\n", shmp->sec, shmp->ns);
	master_exit(0);

	return 0;
}
