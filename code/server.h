/* James M. Rogers */
/* Copyright 2012  */

#include "conn.h"

#define SERVBUFFER	 512		/* the largest message */
#define MAX_CLIENT 5000


typedef void _cb_1 (int);
typedef void _cb_data (Conn *);

typedef void * Server;

Server *ServerNew ();
int ServerDel (Server *);
int SetPort (Server *, int, int);
int SetTLS (Server *, int, const char *, const char *);
void SetCallbacks (Server *, _cb_1 *, _cb_data *, _cb_1 *);
int ServerOpen (Server *);
int ServerClose (Server *);
int ServerLoop (Server *);
int ServerRunning(Server *);
