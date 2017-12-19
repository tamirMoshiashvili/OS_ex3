/*
 * Tamir Moshiashvili
 * 316131259
 * 89-231-03
 * EX3
 */

#include <stdio.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <memory.h>

#define SHM_SIZE 4096

// strings
#define FIFO "fifo_clientTOserver"
#define GAME_OVER_MSG "GAME OVER\n"
#define BLACK_WINNER "Winning player: Black\n"
#define WHITE_WINNER "Winning player: White\n"
#define TIE "No winning player\n"

static void createSharedMemory(int *shmid, char **shmaddr);

static int createFifo(int *fd);

static int handlePlayers(int fd, pid_t *pid1, pid_t *pid2, char *shmaddr);

static void deleteSharedMemory(char *shmaddr, int shmid);

/**
 * write message by perror and exit the program.
 * @param errMsg error message.
 */
static void exitError(char *errMsg) {
    perror(errMsg);
    exit(EXIT_FAILURE);
}

/**
 * delete shared memory and exit the program.
 * @param shmaddr address of shared memory.
 * @param shmid shared memory id.
 */
static void exitAndDeleteMemory(char *shmaddr, int shmid) {
    deleteSharedMemory(shmaddr, shmid);
    exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
    int fd;
    int shmid;
    char *shmaddr = NULL;
    char *msg = NULL;
    pid_t pid1, pid2;
    unsigned int timeToSleep = 1;

    // start of game
    createSharedMemory(&shmid, &shmaddr);
    if (createFifo(&fd) < 0) {
        exitAndDeleteMemory(shmaddr, shmid);
    }
    if (handlePlayers(fd, &pid1, &pid2, shmaddr) < 0) {
        exitAndDeleteMemory(shmaddr, shmid);
    }

    // wait for end of game
    while (*shmaddr != 'e') {
        sleep(timeToSleep);
        timeToSleep = (timeToSleep + 1) % 5;
    }

    // analyze who is the winner
    if (write(1, GAME_OVER_MSG, strlen(GAME_OVER_MSG)) < 0) {
        perror("write failed");
    }
    switch (*(shmaddr + 1)) {
        case 'b':
            msg = BLACK_WINNER;
            break;
        case 'w':
            msg = WHITE_WINNER;
            break;
        default:
            msg = TIE;
    }
    if (write(1, msg, strlen(msg)) < 0) {
        perror("write failed");
    }

    // delete shared memory
    deleteSharedMemory(shmaddr, shmid);
    exit(EXIT_SUCCESS);
}

/**
 * create the shared memory.
 * @param shmid address of int.
 * @param shmaddr address of shared-memory-address.
 */
static void createSharedMemory(int *shmid, char **shmaddr) {
    key_t key;
    // create key
    if ((key = ftok("ex31.c", 'k')) == (key_t) -1) {
        exitError("ftok failed");
    }
    // create memory
    if ((*shmid = shmget(key, SHM_SIZE, IPC_CREAT | IPC_EXCL | 0666)) == -1) {
        exitError("shmget failed");
    }
    // attach
    if ((*shmaddr = shmat(*shmid, NULL, SHM_RDONLY)) == (char *) -1) {
        // delete shared memory
        if (shmctl(*shmid, IPC_RMID, NULL) == -1) {
            exitError("shmctl failed");
        }
        exitError("shmat failed");
    }
}

/**
 * write by perror and return value.
 * @param errMsg error message.
 * @return -1.
 */
static int perror_return(char *errMsg) {
    perror(errMsg);
    return -1;
}

/**
 * create and open fifo.
 * @param fd address of fd.
 * @return 0 on success, -1 otherwise.
 */
static int createFifo(int *fd) {
    // create fifo
    if (mkfifo(FIFO, 0666) == -1) {
        return perror_return("mkfifo failed");
    }
    // open fifo
    if ((*fd = open(FIFO, O_RDONLY)) < 0) {
        return perror_return("open-fifo failed");
    }
    return 0;
}

/**
 * read line from fd to buffer.
 * @param buffer buffer to contain the string.
 * @param fd file descriptor.
 * @return 0 on success, -1 otherwise.
 */
static int readLine(char buffer[], int fd) {
    char c[2];
    c[1] = '\0';
    do {
        if (read(fd, c, 1) == -1) {
            return perror_return("read failed");
        }
        strcat(buffer, c);
    } while (c[0] != '\0');
    return 0;
}

/**
 * read string from fifo which will be pid.
 * @param fd file descriptor of fifo.
 * @param pid address of pid.
 * @return 0 on success, -1 otherwise.
 */
static int handleSinglePlayer(int fd, pid_t *pid) {
    char pidBuffer[16];
    memset(pidBuffer, 0, 16);
    // read till there is a valid pid
    do {
        if (readLine(pidBuffer, fd) < 0) {
            return -1;
        }
        *pid = atoi(pidBuffer);
    } while (*pid == 0);
    return 0;
}

/**
 * close the fifo and delete it.
 * @param fd file descriptor of fifo.
 * @return 0 on success, -1 otherwise.
 */
static int deleteFifo(int fd) {
    if (close(fd) == -1) {
        perror("closing fifo failed");
    }
    if (unlink(FIFO) == -1) {
        return perror_return("unlink to fifo failed");
    }
    return 0;
}

/**
 * connect to players and signal them.
 * @param fd file descriptor of fifo.
 * @param pid1 for first player.
 * @param pid2 for second player.
 * @param shmaddr shared memory.
 * @return 0 on success, -1 otherwise.
 */
static int handlePlayers(int fd, pid_t *pid1, pid_t *pid2, char *shmaddr) {
    // handle first player
    if (handleSinglePlayer(fd, pid1) < 0) {
        deleteFifo(fd);
        return -1;
    }
    // handle second player
    if (handleSinglePlayer(fd, pid2) < 0) {
        deleteFifo(fd);
        return -1;
    }
    // delete fifo
    if (deleteFifo(fd) == -1) {
        return -1;
    }

    // send signals
    if (kill(*pid1, SIGUSR1) == -1) {
        return perror_return("sending signal to process1 failed");
    }
    while (*shmaddr == '\0') {
        // wait for first player to enter his move
        sleep(1);
    }
    if (kill(*pid2, SIGUSR1) == -1) {
        return perror_return("sending signal to process2 failed");
    }
    return 0;
}

/**
 * delete shared memory.
 * @param shmaddr shared memory.
 * @param shmid shared memory id.
 */
static void deleteSharedMemory(char *shmaddr, int shmid) {
    if (shmdt(shmaddr) < 0) {
        exitError("shmdt failed");
    }
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        exitError("shmctl failed");
    }
}
