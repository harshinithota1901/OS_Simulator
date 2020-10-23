#include <unistd.h>

//The variables shared between master and palin processes
struct shared {
	int sec;
	int ns;
	pid_t shmPID;
};

//shared memory constants
#define FTOK_Q_PATH "/tmp"
#define FTOK_SHM_PATH "/tmp"
#define FTOK_Q_KEY 6776
#define FTOK_SHM_KEY 7667

struct msgbuf {
	long mtype;
	pid_t from;
};

enum msg_types{
	CRIT_ANY=0,
	CRIT_LOCK,
	CRIT_UNLOCK,
	CRIT_TERM
};

#define MSG_SIZE sizeof(pid_t)
