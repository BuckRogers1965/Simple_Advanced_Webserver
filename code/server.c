/* James M. Rogers */
/* Copyright 2012  */

#include "server.h"

#define SOCKET_ERROR -1

#include <stdio.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct object {
  int state;
  _cb_1 *callback_new;
  _cb_data *callback_data;
  _cb_1 *callback_close;
  int PrimarySocket;
  int Port;
  int QDepth;
  int Running;
  struct epoll_event ev;
  struct epoll_event *events;
  int epfd;
  void *ssl_ctx;		/* SSL_CTX*, NULL until SetTLS() is called */
  int TLSSocket;		/* second listener for the secure port     */
  int TLSPort;
  Conn *conns[MAX_CLIENT];	/* maps a client fd to its connection obj  */
} object;

void nonblock(int sockfd) {
  int opts;
  opts = fcntl(sockfd, F_GETFL);
  if(opts < 0)
  {
    perror("fcntl(F_GETFL)\n");
    exit(1);
  }
  opts = (opts | O_NONBLOCK);
  if(fcntl(sockfd, F_SETFL, opts) < 0) 
  {
    perror("fcntl(F_SETFL)\n");
    exit(1);
  }
}

Server *
ServerNew () {
  struct object *obj = (struct object *) calloc (sizeof (object), 1);

  /*initialize here if needed */

  return((Server *)obj);
}

int
SetPort (Server * Current, int Port, int QDepth) {
  struct object *obj = (object *) Current;
  obj->Port = Port;
  obj->QDepth = QDepth;
  return 0;
}

void
SetCallbacks (Server * Current, _cb_1 * cbnew, _cb_data * cbdata, _cb_1 * cbclose) {
  struct object *obj = (object *) Current;
  obj->callback_new = cbnew;
  obj->callback_data = cbdata;
  obj->callback_close = cbclose;
}

int
SetTLS (Server * Current, int TLSPort, const char *cert, const char *key) {
  struct object *obj = (object *) Current;

  obj->ssl_ctx = ConnServerCTX (cert, key);
  if (!obj->ssl_ctx)
    return (-1);
  obj->TLSPort = TLSPort;
  return 0;
}

/* Create, bind and listen on a TCP port.  Returns the socket fd, or -1. */
static int
MakeListener (int Port, int QDepth) {
  struct sockaddr_in sin;
  int s;
  int reuse_addr = 1;		/* Used so we can re-bind to our port */

  s = socket (PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == SOCKET_ERROR)
    return (-1);

  /* So that we can re-bind to it without TIME_WAIT problems */
  setsockopt (s, SOL_SOCKET, SO_REUSEADDR, &reuse_addr, sizeof (reuse_addr));

  memset ((char *) &sin, 0, sizeof (sin));
  sin.sin_family = AF_INET;
  sin.sin_port = htons (Port);

  if (bind (s, (struct sockaddr *) &sin, sizeof (sin)) == SOCKET_ERROR)
    {
      close (s);
      return (-1);
    }

  if (listen (s, QDepth) == SOCKET_ERROR)
    {
      close (s);
      return (-1);
    }

  return s;
}

int
ServerOpen (Server * Current) {
  struct object *obj = (object *) Current;

  obj->epfd = epoll_create (MAX_CLIENT);
  if (!obj->epfd)
    {
      perror ("epoll_create\n");
      return (-1);
    }

  obj->PrimarySocket = MakeListener (obj->Port, obj->QDepth);
  if (obj->PrimarySocket < 0)
    return (-2);

  obj->ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
  obj->ev.data.fd = obj->PrimarySocket;
  if (epoll_ctl (obj->epfd, EPOLL_CTL_ADD, obj->PrimarySocket, &obj->ev) < 0)
    {
      perror ("epoll_ctl, failed to add listenfd\n");
      return (-3);
    }

  /* If SetTLS() supplied a certificate, open the secure listener too. */
  if (obj->ssl_ctx)
    {
      obj->TLSSocket = MakeListener (obj->TLSPort, obj->QDepth);
      if (obj->TLSSocket < 0)
	return (-2);

      obj->ev.events = EPOLLIN | EPOLLERR | EPOLLHUP;
      obj->ev.data.fd = obj->TLSSocket;
      if (epoll_ctl (obj->epfd, EPOLL_CTL_ADD, obj->TLSSocket, &obj->ev) < 0)
	{
	  perror ("epoll_ctl, failed to add TLS listenfd\n");
	  return (-3);
	}
    }

  obj->events = (struct epoll_event *) calloc (MAX_CLIENT, sizeof (struct epoll_event));

  obj->Running = 1;
  return 0;
}

int
ServerClose (Server * Current) {
  struct object *obj = (object *) Current;

  if (!obj->Running)
    return (0);

  /* close main socket */
  /* close all client connections */

  free(obj->events);

  return 0;
}

int
ServerLoop (Server * Current) {
  struct object *obj = (object *) Current;

  int clifd;
  int i;
  int res;

  res = epoll_wait (obj->epfd, obj->events, MAX_CLIENT, 1);
  for (i = 0; i < res; i++)
    {
      int active = obj->events[i].data.fd;

      if (active == obj->PrimarySocket ||
	  (obj->ssl_ctx && active == obj->TLSSocket))
	{
	  /* A listener is ready: accept a new connection.  Which listener
	     it was decides whether this connection is plaintext or TLS. */
	  void *ctx = (active == obj->TLSSocket) ? obj->ssl_ctx : NULL;

	  clifd = accept (active, NULL, NULL);
	  if (clifd > 0 && clifd < MAX_CLIENT)
	    {
	      /* we could add code here to allow refuse connection */
	      /* add code here to get client info for connection */

	      /* Wrap the raw fd so nothing downstream has to know whether
	         this connection is plaintext or secure. */
	      obj->conns[clifd] = ConnNew (clifd, ctx);

	      if (*obj->callback_new)
		(obj->callback_new) (clifd);
	      //printf (".");
	      //nonblock (clifd);
	      obj->ev.events = EPOLLIN | EPOLLET;
	      obj->ev.data.fd = clifd;
	      if (epoll_ctl (obj->epfd, EPOLL_CTL_ADD, clifd, &obj->ev) < 0)
		{
		  perror ("epoll_ctl ADD\n");
		  //exit (1);
		}
	    }
	  else if (clifd > 0)
	    {
	      /* more connections than our table can track */
	      close (clifd);
	    }
	}
      else
	{
	  /* An existing client has data.  Hand its connection object off to
	     a worker thread, which owns the read, write and close. */
	  Conn *conn = obj->conns[active];
	  obj->conns[active] = NULL;

	  // remove it from epoll before the worker can close/reuse the fd
	  epoll_ctl (obj->epfd, EPOLL_CTL_DEL, active, NULL);

	  if (*obj->callback_data && conn)
	    (obj->callback_data) (conn);

	  /* we now have to handle the read and close inside the thread. */
	}
    }
  return (0);
}  /* END LOOP */

int ServerRunning(Server * Current) {
  struct object *obj = (object *) Current;
  return (obj->Running);
}

int ServerDel (Server * Current) {

  struct object *obj = (object *) Current;

  ServerClose(Current);
  free(obj);
  Current = NULL;

  return (0);
}


