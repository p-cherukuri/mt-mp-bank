/******************************************************************************
 ** Multithreaded Bank System									                         **
 ** with Multiprocessing server and Shared memory           					**
 ** By: Phanindra Cherukuri <phani.cheruk@gmail.com                          **
 ******************************************************************************/

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>
#include "serverspecs.h"
#include <sys/time.h>

#define EXIT 7
#define FINISH 1
#define BALANCE 2
#define OPEN 3
#define START 4
#define CREDIT 5
#define DEBIT_SUCCESS 10
#define DEBIT_OVERFLOW 11


static int glob_shmid;
static BankServer * glob_shmaddr;
static int glob_sd;
static char current_acc[100];
static int insession = 0;
static int index = 0;


void printBankAccInfo(){ //attempts to lock bank. If successful, prints out information. Happens after a set time interval.
    int i, z;     
 
    sem_wait(&glob_shmaddr->lock);
    if(glob_shmaddr->numberOfAcc == 0){
        write(1, "There are currently zero accounts in the bank.\n", strlen("There are currently zero accounts in the bank.\n"));
	sem_post(&glob_shmaddr->lock);
        return;
    } 
    char message[1500];
    memset(message, 0, 1500);

    char  imessage[20][150]; 
    char * is;
  
    strcat(message, "Interval expired... here is an overview of currently registered bank accounts:\n\n");
    for(i = 0; i < glob_shmaddr->numberOfAcc; i++){
        if(glob_shmaddr->acc[i].flag == 0){
            is = "false";
        }
	else{
	    is = "true";
	}
        sprintf(imessage[i], "Account ID %d: Name = %s, Balance = %f, In Session = %s\n",i, glob_shmaddr->acc[i].name,glob_shmaddr-> acc[i].balance, is);
    }

    for(z = 0; z < glob_shmaddr->numberOfAcc; z ++){
        strcat(message, imessage[z]);
    }
    
    write(1, message, strlen(message) + 1);
   
    sem_post(&glob_shmaddr->lock);
    return;

}

void timeout_handler( int sig) /*blocks signal to ensure that no more than a single interrupt occurs. Then reset*/
{
    sigset_t blockalrm;
    sigemptyset (&blockalrm);
    sigaddset (&blockalrm, SIGALRM);
    sigprocmask (SIG_BLOCK, &blockalrm, NULL); //blocks additional alarm signals from entering
    printBankAccInfo();
    sigprocmask (SIG_UNBLOCK, &blockalrm, NULL);//unblocks alarm signals
    alarm(20);

}

void create_shm() //creates our shared memory
{
    key_t key;
    int shmid;
    
    key = ftok("/server", 'a');

    if ((shmid = shmget(key, sizeof(BankServer), IPC_CREAT | 0666)) < 0)
    {
        perror("shmget");
        exit(1);
    }
    
    glob_shmid = shmid;

    if (*(int*)(glob_shmaddr = shmat(shmid, NULL, 0)) == -1)
    {
        perror("shmat");
        exit(1);
    }

    sem_init(&glob_shmaddr->lock, 1, 1);
    glob_shmaddr->numberOfAcc = 0;
}

void set_shm() //sets our shared memory up
{
    key_t key;
    int shmid;

    key = ftok("/server", 'a');

    if ((shmid = shmget(key, sizeof(BankServer), 0666)) < 0)
    {
        perror("shmget");
        exit(1);
    }

    if (*(int*)(glob_shmaddr = shmat(shmid, NULL, 0)) == -1)
    {
        perror("shmat");
        exit(1);
    }
}
/*Opens a new account - first locks down bank semaphore, then checks if server is at max capacity of account names, or if the account name already exists.
 * If either are true, it returns a statement saying so.
 * If not, account creation is good to go and the account is opened with a semaphore*/
int open_acc(char * acc_name,int  fd){
    char message[256];
    memset(message,0,256);    
    sem_wait(&glob_shmaddr->lock);
    write(fd, message, sprintf(message, "Unlocking bank semaphore for account creation...\n"));
     
    int num = glob_shmaddr->numberOfAcc;
    if(num == 20){ //creation fails
	sem_post(&glob_shmaddr->lock); //give up your lock
	write(fd,message, sprintf(message,"\tSorry! The account database is at full capacity (20), and no further accounts can be created.\n"));
        return -1; 
    }

    for(int i = 0; i < num; i ++){
        if(strcmp(acc_name, glob_shmaddr->acc[i].name) == 0){
            sem_post(&glob_shmaddr->lock);
            write(fd, message, sprintf(message, " Bank account with given name currently exists, try a different name.\n"));
            return -1; 
        }
    }

    sem_init(&glob_shmaddr->acc[num].lock, 1, 1);
    strcpy(glob_shmaddr->acc[num].name, acc_name);
    glob_shmaddr->acc[num].name[strlen(acc_name)] = '\0';
    glob_shmaddr->acc[num].balance = 0;
    glob_shmaddr->acc[num].flag = 0;
    glob_shmaddr->numberOfAcc++;

    sem_post(&glob_shmaddr->lock);
    
    
    return OPEN;
}

int balance(char * reply)
{
    char message[256];
    memset(message, 0, 256);
    if (insession == 0)
        return -1;

    sprintf(message, "Your current balance is %.2f\n", glob_shmaddr->acc[index].balance);
    strcpy(reply, message);
    message[strlen(message)] = '\0';
    return BALANCE;
}

int credit(char * amount){
    int dep_amt;
    if(!insession){
        return -1;
    }

    if ((dep_amt = atof(amount)) == 0)
        return -1;
    
    glob_shmaddr->acc[index].balance = glob_shmaddr->acc[index].balance + dep_amt;
    return CREDIT;  //success
}
/*Withdraws requested amount from account. This can only execute in-session*/
int debit(char * amount){
    int with_amt = 0;
    if(!insession){
        return -1;
    }

    if((with_amt = atof(amount)) == 0)
        return -1;

    if(with_amt > glob_shmaddr->acc[index].balance){ //if the amount being withdrawn is more than the account balance
        return DEBIT_OVERFLOW;
    }

    glob_shmaddr->acc[index].balance = glob_shmaddr->acc[index].balance - with_amt;

    return DEBIT_SUCCESS; //success
}

/* Closes our serving session. Resets flags and frees up the semaphore */
int finish(){
    if(!insession){ //if you aren't currently in service
        return -1; 
    }
    glob_shmaddr->acc[index].flag = 0;
    insession = 0;
    memset(current_acc, 0, sizeof(current_acc));
    sem_post(&glob_shmaddr->acc[index].lock); //release mutex lock
    index = 0;
    return FINISH;
}
/*Attempts to start a session with the account. If another account is already in-session, it will wait and tell the client that it is waiting.
 * Cannot enter session if another account is already in-session, or if the account name doesn't exist*/
int start_acc(char * acc, int fd){
    if (strcmp(acc, current_acc) == 0){
        return -1;
    }
    if(insession == 1){
	return -1; 
    }

    sem_wait(&glob_shmaddr->lock); //bank locked, wait for unlock
    int i;
    if(glob_shmaddr->numberOfAcc == 0){
        write(fd, "There are currently no accounts to start a session with.\n", strlen("There are currently no accounts to start a session with.\n")+ 1);
        sem_post(&glob_shmaddr->lock);
        return -1; 
    } 
    for(i = 0; i < glob_shmaddr->numberOfAcc; i++){
        if(strcmp(acc, glob_shmaddr->acc[i].name) == 0){
            char message[200];
            memset(message,0,200);

            sprintf(message, "Waiting to lock account and start customer session for '%s', please be patient...\n", glob_shmaddr->acc[i].name);
            while(sem_trywait(&glob_shmaddr->acc[i].lock)!=0){
		sem_post(&glob_shmaddr->lock);//unlock
		write(fd, message, strlen(message));
                sleep(3);
		sem_wait(&glob_shmaddr->lock);//re-lock
	    }			 
	   
            
            glob_shmaddr->acc[i].flag = 1;;
            strcpy(current_acc, acc);
            current_acc[strlen(acc)] = '\0';
            insession = 1;
            index = i;
            break; 
        }
    }
    if(i == glob_shmaddr->numberOfAcc){
	write(fd, "That's not a valid account name, try again.\n", strlen("That's not a valid account name, try again.\n"));
	sem_post(&glob_shmaddr->lock);;
	return -1; 
    }   
    sem_post(&glob_shmaddr->lock);
    
    return START;
}

// input parser
int parse_request(char * request, char * reply, int fd)
{
    char parser[150];
    char split[2] = " ";
    char parameter[150];
    char *token;
    int i = 0;
    int request_code;


    if (strcmp(request, "exit") == 0)
        request_code = EXIT;
    else if (strcmp(request, "finish") == 0)
        request_code = FINISH;
    else if (strcmp(request, "balance") == 0)
        request_code = BALANCE;
    else {
        strcpy(parser, request);
        parser[strlen(request)] = '\0';
        
        token = strtok(parser, split);
        
        if (strcmp(parser, "open") == 0) {
            request_code = OPEN;
        } else if (strcmp(parser, "start") == 0) {
            request_code = START;
        } else if (strcmp(parser, "credit") == 0) {
            request_code = CREDIT;
        } else if (strcmp(parser, "debit") == 0) {
            request_code = DEBIT_SUCCESS;
        } else {
            return -5;
        }

        while (token != NULL) {
            i++;
            if (i == 2) {
                strcpy(parameter, token);
                parameter[strlen(token)] = '\0';
            }
            token = strtok(NULL, split);
        }

        if (i > 2 || i <= 1)
            return -5;
    }

    char message[250];
    memset(message, 0, 250);
    switch(request_code)
    {
        case OPEN:
            write(1, message, sprintf(message,"\x1b[2;33mAttempting to open new account... \"%s\"\x1b[0m\n", parameter));
            request_code = open_acc(parameter, fd);
            break;
        case START:
            write(1, message,sprintf(message, "\x1b[2;33Attempting to start account session... \"%s\"\x1b[0m\n", parameter));
            request_code = start_acc(parameter, fd);
            break;
        case BALANCE:
            write(1,message, sprintf(message,"\x1b[2;33mAttempting to retrieve account balance... \"%s\"\x1b[0m\n", current_acc));
            request_code = balance(reply);
            break;
        case CREDIT:
            request_code = credit(parameter);
            break;
        case DEBIT_SUCCESS:
            request_code = debit(parameter);
            break;
        case FINISH:
            request_code = finish();
            break;
    }

    return request_code; 
}


int client_session_actions(int sd)
{
    char request[512];
    char reply[512];
    char message[256];
    int request_code;
    memset(reply, 0, 512);
    glob_sd = sd;
    
    while ( read( sd, request, sizeof(request) ) > 0 )
    {
        printf( "==>Server receives input:  %s\n", request );

        if ((request_code = parse_request(request, reply, sd)) == -5) {
            write(sd, reply, sprintf(reply, "\x1b[1;31mIncorrect input: please try again\x1b[0m\n"));
        } else {
            switch(request_code)
            {
                case BALANCE:
                    write( sd, reply, strlen(reply) + 1 );
                    break;
                case DEBIT_SUCCESS:
                    write(sd, reply, sprintf(reply, "Amount successfully withdrawn.\n"));
                    break;
                case DEBIT_OVERFLOW:
                    write(sd, reply, sprintf(reply, "Invalid debit request - your balance is too low, try a different amount.\n"));
                    break;
                case -1:
                    write(sd, reply, sprintf(reply, "Option request \"%s\" failed.\n", request));
                    break;
                default:
                    write(sd, reply, sprintf(reply, "Option request \"%s\" succeeded!\n", request));
            }
        }
        
        if (request_code == EXIT) {
	    if(insession){ //release semaphore
		sem_post(&glob_shmaddr->acc[index].lock);
		glob_shmaddr->acc[index].flag = 0;
	    }
            write(1, message, sprintf(message, "Ending child process %d\n", getpid()));
            exit(0);
        }
    }
    close( sd );
    return 0;
}



void sigchld_handler(int s)
{
    int pid;
    while((pid = waitpid(-1, NULL, WNOHANG)) > 0) {
        printf("Killing child process %d\n", pid);
    }
}

// SIGINT
void sigint_handler(int sig)
{   
    if (shmdt(glob_shmaddr) != 0) { //clear up shared memory
        perror("shmdt");
        exit(1);
    }

    shmctl(glob_shmid, IPC_RMID, NULL);
    write(glob_sd, "disconnect", 11); //kill client process
    exit(0);
}

int main( int argc, char ** argv )
{
    int sd;
    char message[256];
    pthread_attr_t kernel_attr;
    socklen_t ic;
    int	fd;
    struct sockaddr_in senderAddr;
    pid_t child_pid;

    struct sigaction sigint_action;
    struct sigaction sigchld_action;
    struct sigaction sigalrm_action;

    sigalrm_action.sa_handler = timeout_handler;
    sigalrm_action.sa_flags = SA_RESTART; 
    sigaction(SIGALRM, &sigalrm_action, NULL);;
    alarm(3);

    sigint_action.sa_handler = sigint_handler;
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;

    if (sigaction(SIGINT, &sigint_action, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    sigchld_action.sa_handler = sigchld_handler; // zombie processes
    sigemptyset(&sigchld_action.sa_mask);
    sigchld_action.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sigchld_action, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
   ;
   
 
    if ( pthread_attr_init( &kernel_attr ) != 0 )
    {
        printf( "pthread_attr_init() failed in file %s line %d\n", __FILE__, __LINE__ );
        return 0;
    }
    else if ( pthread_attr_setscope( &kernel_attr, PTHREAD_SCOPE_SYSTEM ) != 0 )
    {
        printf( "pthread_attr_setscope() failed in file %s line %d\n", __FILE__, __LINE__ );
        return 0;
    }
    else if ( (sd = claim_port( "51628" )) == -1 )
    {
        write( 1, message, sprintf( message,  "\x1b[1;31mCould not bind to port %s errno %s\x1b[0m\n", "51268", strerror( errno ) ) );
        return 1;
    }
    else if ( listen( sd, 100 ) == -1 )
    {
        printf( "listen() failed in file %s line %d\n", __FILE__, __LINE__ );
        close( sd );
        return 0;
    }
    else if (create_shm(), glob_shmaddr == NULL) //create shared memory
    {
        printf("NULL shared mem returned when creating\n");
        return 0;
    }
    else
    {
        write(1, message, sprintf(message, "\x1b[1;32mShared memory successfully created.\x1b[0m\n"));
        ic = sizeof(senderAddr);
        while ( (fd = accept( sd, (struct sockaddr *)&senderAddr, &ic )) != -1 )
        {
            write(1, message, sprintf(message, "\x1b[1;32mClient accepted at file descriptor %d .\x1b[0m\n", fd));
            child_pid = fork();
            if (child_pid >= 0) {
                if (child_pid == 0) //know this is child processes
                {
		    sigalrm_action.sa_handler = SIG_IGN; //ignore timer calls in child process
		    close(sd);
                    client_session_actions(fd);
                    close(fd);
                    
                }
                else //this is the parent process
                {
                    write(1, message, sprintf(message, "\x1b[1;32mCreated child process %d\x1b[0m\n", child_pid));
                    close(fd);
                }
            }
        }
        close( sd );
        return 0;
    }
}

int claim_port( const char * port )
{
    struct addrinfo addrinfo;
    struct addrinfo *	result;
    int	sd;
    char message[256];
    int on = 1;

    addrinfo.ai_flags = AI_PASSIVE;		// bind()
    addrinfo.ai_family = AF_INET;		// IPv4 only
    addrinfo.ai_socktype = SOCK_STREAM;	// TCP/IP
    addrinfo.ai_protocol = 0;		    // Any protocol
    addrinfo.ai_addrlen = 0;
    addrinfo.ai_addr = NULL;
    addrinfo.ai_canonname = NULL;
    addrinfo.ai_next = NULL;
    if ( getaddrinfo( 0, port, &addrinfo, &result ) != 0 ) // port 3000
    {
        fprintf( stderr, "\x1b[1;31mgetaddrinfo( %s ) failed errno is %s.  File %s line %d.\x1b[0m\n", port, strerror( errno ), __FILE__, __LINE__ );
        return -1;
    }
    else if ( errno = 0, (sd = socket( result->ai_family, result->ai_socktype, result->ai_protocol )) == -1 )
    {
        write( 1, message, sprintf( message, "socket() failed.  File %s line %d.\n", __FILE__, __LINE__ ) );
        freeaddrinfo( result );
        return -1;
    }
    else if ( setsockopt( sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on) ) == -1 )
    {
        write( 1, message, sprintf( message, "setsockopt() failed.  File %s line %d.\n", __FILE__, __LINE__ ) );
        freeaddrinfo( result );
        close( sd );
        return -1;
    }
    else if ( bind( sd, result->ai_addr, result->ai_addrlen ) == -1 )
    {
        freeaddrinfo( result );
        close( sd );
        write( 1, message, sprintf( message, "\x1b[2;33mBinding to port %s ...\x1b[0m\n", port ) );
        return -1;
    }
    else
    {
        write( 1, message, sprintf( message,  "\x1b[1;32mSUCCESS : Bind to port %s\x1b[0m\n", port ) );
        freeaddrinfo( result );		
        return sd; // bind() success;
    }
}

