/******************************************************************************
 ** Multithreaded Bank System									                         **
 ** with Multiprocessing server and Shared memory           					**
 ** By: Phanindra Cherukuri <phani.cheruk@gmail.com                          **
 ******************************************************************************/

#ifndef SERVER_H
#define SERVER_H 

#include <semaphore.h>

typedef struct CustomerAcc
{
    sem_t lock;
    int flag;
    char name[100];
    float balance;
} CustomerAcc;

typedef struct BankServer
{
    sem_t lock;
    int numberOfAcc;
    CustomerAcc acc[20];
} BankServer;

void create_shm();

void get_shm();

int claim_port(const char * port);

int client_session_actions(int sd);

int parse_request(char * request, char * reply, int fd);

#endif


