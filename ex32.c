/*
 * Tamir Moshiashvili
 * 316131259
 * 89-231-03
 * EX3
 */

#include <stdio.h>
#include <unistd.h>
#include <memory.h>
#include <signal.h>
#include <sys/ipc.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/shm.h>

// bool
#define TRUE 1
#define FALSE 0

// buffer size
#define SHM_SIZE 4096
#define MOVE_SIZE 8

#define IS_END_OF_GAME(numFreeCells, numBlack, numWhite) \
 (numFreeCells == 0 || (BOARD_SIZE * BOARD_SIZE - numFreeCells) == numBlack \
|| (BOARD_SIZE * BOARD_SIZE - numFreeCells) == numWhite)

// board
#define BOARD_SIZE 8
#define FIRST_MID 3
#define SECOND_MID 4

// strings
#define FIFO "fifo_clientTOserver"
#define WAIT_MSG "Waiting for the other player to make a move\n"
#define CHOOSE_SQUARE "Please choose a square\n"
#define BLACK_WINNER "Winning player: Black\n"
#define WHITE_WINNER "Winning player: White\n"
#define TIE "No winning player\n"
#define NO_SQUARE "No such square\nPlease choose another square\n"
#define INVALID_SQUARE "This square is invalid\nPlease choose another square\n"

enum token {
    NEUTRAL,
    WHITE,
    BLACK
};

// static vars
static int shmid;
static char *shmaddr = NULL;

static void initBoard(int board[BOARD_SIZE][BOARD_SIZE]);

static void setHandler();

static void writePid();

static void analyzeWhichPlayer(int *myTok, char *color, int *rivalTok);

static int rivalPlay(char move[MOVE_SIZE],
                     int board[BOARD_SIZE][BOARD_SIZE], int rivalTok);

static void updateAndPrint(int token, int numChangedTokens,
                           int *numFreeCells, int *numBlack, int *numWhite,
                           int board[BOARD_SIZE][BOARD_SIZE]);

static int play(char move[MOVE_SIZE],
                int board[BOARD_SIZE][BOARD_SIZE], int myTok);

static void writeMoveToShm(char move[MOVE_SIZE], char color);

static char printEndMsg(int numBlack, int numWhite);

static int makeMove(int board[BOARD_SIZE][BOARD_SIZE],
                    int row, int col, int token);

/**
 * write by perror and exit with failure.
 * @param errMsg error message.
 */
static void perrorExit(char *errMsg) {
    perror(errMsg);
    exit(EXIT_FAILURE);
}

/**
 * write to stdout.
 * @param str string to write.
 * @param len length of string.
 */
static void writeStr(char *str, size_t len) {
    if (write(1, str, len) < 0) {
        perrorExit("write failed");
    }
}

int main(int argc, char **argv) {
    int board[BOARD_SIZE][BOARD_SIZE];
    int numFreeCells = BOARD_SIZE * BOARD_SIZE - 4;
    int numWhite = 2, numBlack = 2;

    int myTok = NEUTRAL, rivalTok = NEUTRAL;
    char color;
    char move[MOVE_SIZE];
    int numChangedTokens = 0;

    // init
    memset(move, 0, MOVE_SIZE);
    initBoard(board);
    setHandler();
    writePid();
    pause();
    analyzeWhichPlayer(&myTok, &color, &rivalTok);

    // check for first turn of white player
    if (color == 'w') {
        numChangedTokens = rivalPlay(move, board, rivalTok);
        updateAndPrint(rivalTok, numChangedTokens,
                       &numFreeCells, &numBlack, &numWhite, board);
    }

    do {
        // player's turn
        writeStr(CHOOSE_SQUARE, strlen(CHOOSE_SQUARE));
        numChangedTokens = 0;
        while (numChangedTokens == 0) {
            numChangedTokens = play(move, board, myTok);
        }
        updateAndPrint(myTok, numChangedTokens,
                       &numFreeCells, &numBlack, &numWhite, board);
        writeMoveToShm(move, color);
        // check for end of game
        if (IS_END_OF_GAME(numFreeCells, numBlack, numWhite)) {
            printEndMsg(numBlack, numWhite);
            exit(EXIT_SUCCESS);
        }

        // wait for player move
        writeStr(WAIT_MSG, strlen(WAIT_MSG));
        while (*shmaddr == color) {
            sleep(1);
        }
        // got new move, update the board
        numChangedTokens = rivalPlay(move, board, rivalTok);
        updateAndPrint(rivalTok, numChangedTokens,
                       &numFreeCells, &numBlack, &numWhite, board);
        // check for end of game
        if (IS_END_OF_GAME(numFreeCells, numBlack, numWhite)) {
            // write to shared memory the details about the players
            *(shmaddr + 1) = printEndMsg(numBlack, numWhite);
            *shmaddr = 'e';
            exit(EXIT_SUCCESS);
        }
    } while (TRUE);
}

/**
 * initialize the board.
 * @param board matrix.
 */
static void initBoard(int board[BOARD_SIZE][BOARD_SIZE]) {
    int i, j;
    // fill with zero
    for (i = 0; i < BOARD_SIZE; ++i) {
        for (j = 0; j < BOARD_SIZE; ++j) {
            board[i][j] = NEUTRAL;
        }
    }
    // place static squares
    board[FIRST_MID][FIRST_MID] = board[SECOND_MID][SECOND_MID] = BLACK;
    board[SECOND_MID][FIRST_MID] = board[FIRST_MID][SECOND_MID] = WHITE;
}

/**
 * signal handler.
 * @param signum signal number.
 */
void usr1_handle(int signum) {
    key_t key;
    // get key
    if ((key = ftok("ex31.c", 'k')) == (key_t) -1) {
        perrorExit("ftok failed");
    }
    // get shared memory
    if ((shmid = shmget(key, SHM_SIZE, 0666)) == -1) {
        perrorExit("shmget failed");
    }
    // attach
    if ((shmaddr = shmat(shmid, NULL, 0)) == (char *) -1) {
        perrorExit("shmat failed");
    }
}

/**
 * set signal handler for SIGUSR1.
 */
static void setHandler() {
    struct sigaction usr1_act;
    sigset_t block_mask;
    sigfillset(&block_mask);
    usr1_act.sa_handler = usr1_handle;
    usr1_act.sa_mask = block_mask;
    usr1_act.sa_flags = 0;
    if (sigaction(SIGUSR1, &usr1_act, NULL) < 0) {
        perrorExit("sigaction failed");
    }
}

/**
 * write the proccess's pid to the fifo.
 */
static void writePid() {
    int fd;
    char buffer[16];
    memset(buffer, 0, 16);
    // open fifo
    if ((fd = open(FIFO, O_WRONLY)) < 0) {
        perrorExit("opening fifo failed");
    }
    // write pid
    sprintf(buffer, "%d", getpid());
    if (write(fd, buffer, strlen(buffer) + 1) < 0) {
        if (close(fd) < 0) {
            perrorExit("closing fifo failed");
        }
        perrorExit("writing failed");
    }
    // close fifo
    if (close(fd) < 0) {
        perrorExit("closing fifo failed");
    }
}

/**
 * analyze which player are you.
 * @param myTok my token number.
 * @param color my token color.
 * @param rivalTok rival token number.
 */
static void analyzeWhichPlayer(int *myTok, char *color, int *rivalTok) {
    if (*shmaddr == '\0') {
        // first player will find '\0' at shared memory --> black
        *color = 'b';
        *myTok = BLACK;
        *rivalTok = WHITE;
    } else {
        // second player will find 'b' at shared memory --> white
        *color = 'w';
        *myTok = WHITE;
        *rivalTok = BLACK;
    }
}

/**
 * apply rival's move.
 * @param move string to represent the move.
 * @param board play-board.
 * @param rivalTok rival token number.
 * @return number of token that was affected (changed) by the move.
 */
static int rivalPlay(char move[MOVE_SIZE],
                     int board[BOARD_SIZE][BOARD_SIZE], int rivalTok) {
    int row, col;
    // extract column
    strncpy(move, shmaddr + 1, 1);
    move[1] = '\0';
    col = atoi(move);
    // extract row
    strncpy(move, shmaddr + 2, 1);
    row = atoi(move);
    // apply the move
    return makeMove(board, row, col, rivalTok);
}

/**
 * rotate the matrix 90 degrees clockwise.
 * @param board play-board.
 */
static void rotateMatrix90DegreesClockwise(int board[BOARD_SIZE][BOARD_SIZE]) {
    int halfBoardSize = BOARD_SIZE / 2;
    int i, j, temp;
    for (i = 0; i < halfBoardSize; ++i) {
        for (j = i; j < BOARD_SIZE - 1 - i; ++j) {
            temp = board[i][j];
            // right to top
            board[i][j] = board[j][BOARD_SIZE - 1 - i];
            // bottom to right
            board[j][BOARD_SIZE - 1 - i] =
                    board[BOARD_SIZE - 1 - i][BOARD_SIZE - 1 - j];
            // left to bottom
            board[BOARD_SIZE - 1 - i][BOARD_SIZE - 1 - j] =
                    board[BOARD_SIZE - 1 - j][i];
            // temp to left
            board[BOARD_SIZE - 1 - j][i] = temp;
        }
    }
}

/**
 * apply the move, if possible, to the right horizontal row.
 * @param board play-board.
 * @param row row number.
 * @param col column number.
 * @param token token number.
 * @return number of changed tokens.
 */
static int rightHorizontalSqaures(int board[BOARD_SIZE][BOARD_SIZE],
                                  int row, int col, int token) {
    int j, numChangedTokens = 0;
    // right -->
    for (j = col + 1; j < BOARD_SIZE; ++j) {
        if (board[row][j] == NEUTRAL) {
            // this direction is not valid
            return 0;
        } else if (board[row][j] == token) {
            // found valid square
            numChangedTokens = j - col - 1;
            // change tokens
            for (++col; col < j; ++col) {
                board[row][col] = token;
            }
            break;
        }
    }
    return numChangedTokens;
}

/**
 * apply the move, if possible, to the right-down diagonal squares.
 * @param board play-board.
 * @param row row number.
 * @param col column number.
 * @param token token number.
 * @return number of changed tokens.
 */
static int rightDiagonalSquares(int board[BOARD_SIZE][BOARD_SIZE],
                                int row, int col, int token) {
    // right and down
    int i, j, numChangedTokens = 0;
    for (i = row + 1, j = col + 1; i < BOARD_SIZE && j < BOARD_SIZE; i++, j++) {
        if (board[i][j] == NEUTRAL) {
            // this direction is not valid
            return numChangedTokens;
        } else if (board[i][j] == token) {
            // found valid square
            numChangedTokens = j - col - 1;
            // change tokens
            for (++row, ++col; row < i && col < j; ++row, ++col) {
                board[row][col] = token;
            }
            break;
        }
    }
    return numChangedTokens;
}

/**
 * apply the move on the board.
 * @param board play-board.
 * @param row row number.
 * @param col column number.
 * @param token token number.
 * @return number of changed tokens.
 */
static int makeMove(int board[BOARD_SIZE][BOARD_SIZE],
                    int row, int col, int token) {
    int i, temp, numChangedTokens = 0;

    // check for valid move
    if (row < 0 || col < 0 || row >= BOARD_SIZE || col >= BOARD_SIZE) {
        writeStr(NO_SQUARE, strlen(NO_SQUARE));
        return numChangedTokens;
    } else if (board[row][col] != NEUTRAL) {
        writeStr(INVALID_SQUARE, strlen(INVALID_SQUARE));
        return numChangedTokens;
    }

    // try all possible states
    for (i = 0; i < 4; ++i) {
        rotateMatrix90DegreesClockwise(board);
        // rotate row and col
        temp = row;
        row = BOARD_SIZE - col - 1;
        col = temp;
        // make changes
        numChangedTokens += rightHorizontalSqaures(board, row, col, token);
        numChangedTokens += rightDiagonalSquares(board, row, col, token);
    }
    if (numChangedTokens == 0) {
        // invalid squares
        writeStr(INVALID_SQUARE, strlen(INVALID_SQUARE));
        return FALSE;
    }
    // valid move, place the token
    board[row][col] = token;
    return numChangedTokens; //todo
}

/**
 * update number of token of each.
 * @param token type of token that affected the board.
 * @param numChangedTokens number of tokens that was changed.
 * @param numFreeCells number of free cells.
 * @param numBlack number of black squares.
 * @param numWhite number of white squares.
 */
static void updateTokens(int token, int numChangedTokens,
                         int *numFreeCells, int *numBlack, int *numWhite) {
    switch (token) {
        case BLACK:
            *numBlack += numChangedTokens + 1;
            *numWhite -= numChangedTokens;
            break;
        case WHITE:
            *numBlack -= numChangedTokens;
            *numWhite += numChangedTokens + 1;
            break;
        default:
            break;
    }
    *numFreeCells -= 1;
}

/**
 * print the board to the screen.
 * @param board play-board.
 */
static void printBoard(int board[BOARD_SIZE][BOARD_SIZE]) {
    int i, j;
    char c, *msg = "The board is:\n";
    writeStr(msg, strlen(msg));
    for (i = 0; i < BOARD_SIZE; ++i) {
        for (j = 0; j < BOARD_SIZE - 1; ++j) {
            c = (char) (board[i][j] + 48);
            writeStr(&c, 1);
            writeStr(" ", 1);
        }
        // last square in the row
        c = (char) (board[i][j] + 48);
        writeStr(&c, 1);
        writeStr("\n", 1);
    }
}

/**
 * update token numbers and print the board.
 * @param token token number that affected the board.
 * @param numChangedTokens numbre of tokens that was changed.
 * @param numFreeCells number of free cells.
 * @param numBlack number of black squares.
 * @param numWhite number of white squares.
 * @param board play-board.
 */
static void updateAndPrint(int token, int numChangedTokens,
                           int *numFreeCells, int *numBlack, int *numWhite,
                           int board[BOARD_SIZE][BOARD_SIZE]) {
    updateTokens(token, numChangedTokens,
                 numFreeCells, numBlack, numWhite);
    printBoard(board);
}

/**
 * write the given move to the shared memory.
 * @param move string that represents the move.
 * @param color color of player.
 */
static void writeMoveToShm(char move[MOVE_SIZE], char color) {
    // change '[' to color
    *move = color;
    // transfer row to ','
    move[2] = move[3];
    move[3] = '\0';
    // write to shared memory
    strncpy(shmaddr, move, strlen(move));
}

/**
 * read line from stdin.
 * @param buffer buffer to contain the string.
 */
static void readLineFromStdin(char buffer[]) {
    char c[2];
    c[1] = '\0';

    buffer[0] = '\0';
    do {
        if (read(0, c, 1) < 0) {
            perrorExit("read failed");
        }
        if (c[0] != '\n') {
            strcat(buffer, c);
        } else {
            break;
        }
    } while (TRUE);
}

/**
 * extract the number from the string that writen from 'start' to 'end'.
 * @param str string.
 * @param start start of number.
 * @param end end of number.
 * @return number.
 */
static int extractNumber(char *str, char start, char end) {
    char buffer[16];
    char *strStart = strchr(str, start);
    unsigned long length = strchr(str, end) - strStart - 1;
    strncpy(buffer, strStart + 1, length);
    buffer[length] = '\0';
    return atoi(buffer);
}

/**
 * get move from user and apply it on board.
 * @param move string representing the move.
 * @param board play-board.
 * @param myTok my toke number.
 * @return number of changed tokens.
 */
static int play(char move[MOVE_SIZE],
                int board[BOARD_SIZE][BOARD_SIZE], int myTok) {
    int row, col;
    readLineFromStdin(move);
    col = extractNumber(move, '[', ',');
    row = extractNumber(move, ',', ']');
    return makeMove(board, row, col, myTok);
}

/**
 * print end-of-game message.
 * @param numBlack number of black tokens.
 * @param numWhite number of white tokens.
 * @return char to represent the winner (or a tie).
 */
static char printEndMsg(int numBlack, int numWhite) {
    char *msg = NULL;
    char state;
    if (numBlack > numWhite) {
        msg = BLACK_WINNER;
        state = 'b';
    } else if (numWhite > numBlack) {
        msg = WHITE_WINNER;
        state = 'w';
    } else {
        msg = TIE;
        state = 't';
    }
    // write the message to screen
    writeStr(msg, strlen(msg));
    return state;
}
