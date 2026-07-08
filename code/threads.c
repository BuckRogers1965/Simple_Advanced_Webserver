#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <pthread.h>

#include "conn.h"
#include "response.h"

typedef struct thread
{
    int Running;
    Conn *conn;
    pthread_t thread;
    char buffer[1024];
    struct thread *next;
    struct thread *prev;
} thread;

struct thread *freelist;
struct thread *worklist;
pthread_mutex_t freelist_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t worklist_mutex = PTHREAD_MUTEX_INITIALIZER;

void *handleRequest(void *arg)
{
    struct thread *t = (thread *)arg;
    int n;

    for (;;)
    {
        while (!t->Running)
        {
            usleep(100);
        }

        // Complete the TLS handshake (a no-op for plaintext connections)
        // here in the worker, so it stays off the main epoll loop.
        if (ConnAccept(t->conn))
        {
            n = ConnRead(t->conn, t->buffer, 1023);

            if (n > 0)
            { // got a request, process it.
                HandleResponse(t->conn, t->buffer, n);
            }
        }

        ConnClose(t->conn);
        t->conn = NULL;
        t->Running = 0;

        pthread_mutex_lock(&worklist_mutex);
        if (worklist == t)
            worklist = t->next;
        if (t->next)
            t->next->prev = t->prev;
        if (t->prev)
            t->prev->next = t->next;
        pthread_mutex_unlock(&worklist_mutex);

        pthread_mutex_lock(&freelist_mutex);
        t->next = freelist;
        if (freelist)
            freelist->prev = t;
        freelist = t;
        pthread_mutex_unlock(&freelist_mutex);
    }
}

void ThreadInitialize(int ThreadCount)
{
    struct thread *t;
    int i;

    freelist = worklist = NULL;

    for (i = 0; i < ThreadCount; i++)
    {
        t = (struct thread *)calloc(1, sizeof(thread));

        pthread_mutex_lock(&freelist_mutex);
        // add new thread structure to freelist
        t->next = freelist;
        freelist = t;
        pthread_mutex_unlock(&freelist_mutex);

        if (pthread_create(&t->thread, NULL, &handleRequest, t) != 0)
        {
            perror("pthread create failed\n");
            exit(1);
        }
    }
}

void ThreadCheck()
{
    pthread_mutex_lock(&worklist_mutex);
    struct thread *t = worklist;
    while (t != NULL)
    {
        if (!t->Running)
        {
            // remove thread structure from worklist
            if (worklist == t)
                worklist = t->next;
            if (t->next)
                t->next->prev = t->prev;
            if (t->prev)
                t->prev->next = t->next;

            pthread_mutex_unlock(&worklist_mutex);
            pthread_mutex_lock(&freelist_mutex);
            // add thread structure to freelist
            t->next = freelist;
            if (freelist)
                freelist->prev = t;
            freelist = t;
            pthread_mutex_unlock(&freelist_mutex);
            return;
        }
        t = t->next;
        usleep(10);
    }
    pthread_mutex_unlock(&worklist_mutex);
}

void ThreadHandleResponse(Conn *conn)
{
    struct thread *t;

    /* wait for any child process to free up */
    while (1)
    {

        if (freelist != NULL)
        {
            pthread_mutex_lock(&freelist_mutex);
            // remove thread structure from freelist
            t = freelist;
            freelist = t->next;
            if (freelist)
                freelist->prev = NULL;
            pthread_mutex_unlock(&freelist_mutex);

            pthread_mutex_lock(&worklist_mutex);
            // add thread structure to worklist
            t->next = worklist;
            if (worklist)
                worklist->prev = t;
            worklist = t;
            pthread_mutex_unlock(&worklist_mutex);

            // setup thread and run it
            t->conn = conn;
            t->Running = 1;
            return;
        }
        pthread_mutex_unlock(&freelist_mutex);

        ThreadCheck();
        sched_yield();
    }
}
