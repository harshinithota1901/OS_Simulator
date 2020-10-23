#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <time.h>
#include <stdlib.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>
#include "master.h"

static int shmid = -1, msgid = -1;  //semaphore id
static struct shared * shmp = NULL;

//Initialize shared memory pointer
static int shared_initialize()
{
	key_t key = ftok(FTOK_SHM_PATH, FTOK_SHM_KEY);  //get a key for the shared memory
	if(key == -1){
		perror("ftok");
		return -1;
	}

	shmid = shmget(key, 0, 0);
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

	msgid = msgget(key, 0);
	if(msgid == -1){
		perror("msgget");
		return EXIT_FAILURE;
	}

	return 0;
}

static int send_msg(const int msgid, struct msgbuf *m, enum msg_types t){
	m->mtype = t;
	m->from = getpid();	//mark who is sending the message
	if(msgsnd(msgid, m, MSG_SIZE, 0) == -1){
		perror("msgsnd");
		return EXIT_FAILURE;
	}

	//wait for reply
	if(msgrcv(msgid, (void*)m, MSG_SIZE, m->from, 0) == -1){
		perror("user:msgrcv");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

int main(const int argc, char * const argv[]){

	struct msgbuf msg;
	int end_sec, end_ns;	//time when we have to exit

	if(shared_initialize() < 0){
		return EXIT_FAILURE;
	}

	//initialize the rand() function
	srand(getpid());

	//enter critical section to save start time
	if(send_msg(msgid, &msg, CRIT_LOCK) == EXIT_FAILURE){	//lock shared oss clock
		return EXIT_FAILURE;
	}

	const int runtime = 1 + (rand() % (100000-1));

	end_ns = shmp->ns + runtime;
	if(end_ns > 1000000000){
		end_ns %= 1000000000;
		end_sec = shmp->sec + 1;
	}else{
		end_sec = shmp->sec;
	}

	//exit critical section after start time is copied
	if(send_msg(msgid, &msg, CRIT_UNLOCK) == EXIT_FAILURE){	//unlock shared oss clock
		return EXIT_FAILURE;
	}

	int past_runtime = 0;
	while(past_runtime == 0){
		//send request to enter critical section to master
		if(send_msg(msgid, &msg, CRIT_LOCK) == EXIT_FAILURE){	//lock shared oss clock
			break;
		}

		if(	(end_sec > shmp->sec) ||
				((end_sec == shmp->sec) && (end_ns >= shmp->ns)) ){

			if(shmp->shmPID == 0){
				shmp->shmPID = getpid();
				past_runtime = 1;
			}
		}

		if(send_msg(msgid, &msg, CRIT_UNLOCK) == EXIT_FAILURE){	//unlock shared oss clock
			break;
		}
	}

	//tell master we exit
	msg.from = getpid();
	send_msg(msgid, &msg, CRIT_TERM);

	shmdt(shmp);
	return EXIT_SUCCESS;
}
