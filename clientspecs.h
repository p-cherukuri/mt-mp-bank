/******************************************************************************
 ** Multithreaded Bank System									                         **
 ** with Multiprocessing server and Shared memory           					**
 ** By: Phanindra Cherukuri <phani.cheruk@gmail.com                          **
 ******************************************************************************/

#ifndef CLIENT_H
#define CLIENT_H

void * operation_input_thread(void * arg);

void * reply_output_thread(void * arg);

int server_connection(const char * server, const char * port);

int thread_creation(int sd);

#endif

