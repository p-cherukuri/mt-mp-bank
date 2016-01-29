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
#include "clientspecs.h"

#define EXIT "exit"
#define FINISH "finish"

void * operation_input_thread( void * arg )
{
    int	sd;
    printf("\nPlease choose an option from the following list:\n\nUser Client Options:\n'open <accountname>' - Create a new customer account.\n'start <accountname>' - Start a session with an existing customer account.\n'finish' - Close the in-session customer account.\n'exit' - Disconnect from our bank server and end your client process.\n\nIn-session Customer Options:\n'balance' - Show the current balance of the in-session customer account.\n'credit <amount>' - Make a deposit to the in-session account.\n'debit <amount>' - Withdraw money from the in-session account.\n\n");
    char prompt[] = "Enter an option >>";
    char request[2048];
    int len;

    sd = *(int *)arg;
    while ( write( 1, prompt, sizeof(prompt)), (len = read( 0, request, sizeof(request))) > 0)
    {
        request[len-1]= '\0';
        
        if (strcmp(request, EXIT) == 0) {
            printf("Exiting..\n");
            write(sd, "exit", 5);
            return 0;
        }
        
        printf( "<== sending request to server: %s\n", request );
        write( sd, request, strlen(request) + 1 );
        sleep(2);
    }

    return 0;
}
void * reply_output_thread( void * arg )
{
    int	sd;
    char reply[2048];
    char message[2048];
    pthread_detach(pthread_self());

    sd = *(int *)arg;
    while ( read( sd, reply, sizeof(reply) ) > 0 )
    {
        write(1, message,sprintf(message, "==> receiving reply from server:  %s\n", reply));
        if (strcmp(reply, "disconnect") == 0)

            exit(0);
    }
    return 0;
}

int server_connection( const char * server, const char * port )
{
    int	sd;
    struct addrinfo addrinfo;
    struct addrinfo * result;
    char message[256];

    addrinfo.ai_flags = 0;
    addrinfo.ai_family = AF_INET;		// IPv4 only
    addrinfo.ai_socktype = SOCK_STREAM;	// Want TCP/IP
    addrinfo.ai_protocol = 0;		// Any protocol
    addrinfo.ai_addrlen = 0;
    addrinfo.ai_addr = NULL;
    addrinfo.ai_canonname = NULL;
    addrinfo.ai_next = NULL;
    if ( getaddrinfo( server, port, &addrinfo, &result ) != 0 )
    {
        fprintf( stderr, "\x1b[1;31mgetaddrinfo( %s ) failed.  File %s line %d.\x1b[0m\n", server, __FILE__, __LINE__ );
        return -1;
    }
    else if ( errno = 0, (sd = socket( result->ai_family, result->ai_socktype, result->ai_protocol )) == -1 )
    {
        freeaddrinfo( result );
        return -1;
    }
    else
    {
        do {
            if ( errno = 0, connect( sd, result->ai_addr, result->ai_addrlen ) == -1 )
            {
                sleep( 3 );
                write( 1, message, sprintf( message, "\x1b[2;33mConnecting to the server %s ...\x1b[0m\n", server ) );
            }
            else
            {
                freeaddrinfo( result );
                return sd;		// connect() succeeded
            }
        } while ( errno == ECONNREFUSED );
        freeaddrinfo( result );
        return -1;
    }
}

int thread_creation(int sd)
{
    pthread_t tid[2];
    pthread_attr_t kernel_attr;
    int * fdptr;

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
    else
    {
        fdptr = (int *)malloc(sizeof(int *));
        *fdptr = sd;

        if (pthread_create( &tid[0], &kernel_attr, operation_input_thread, fdptr ) != 0) {
            printf( "pthread_create() failed in file %s line %d\n", __FILE__, __LINE__ );
            return 0;
        }

        if (pthread_create( &tid[1], &kernel_attr, reply_output_thread, fdptr ) != 0) {
            printf( "pthread_create() failed in file %s line %d\n", __FILE__, __LINE__ );
            return 0;
        }

        pthread_join(tid[0], NULL);

        free(fdptr);
    }
    return 0;
}

int main( int argc, char ** argv )
{
    int	sd;

    if ( argc < 2 )
    {
        fprintf( stderr, "\x1b[1;31mHost name is not specified.  File %s line %d.\x1b[0m\n", __FILE__, __LINE__ );
        exit( 1 );
    }
    while ((sd = server_connection( argv[1], "51628" )) == -1 )
        sleep(3);


    printf( "Connected to server. %s\n", argv[1] );
    printf( "\nWelcome to the Iron Bank of Multithreading! How can we help you today?");
    thread_creation(sd);
    close( sd );
    return 0;
}
