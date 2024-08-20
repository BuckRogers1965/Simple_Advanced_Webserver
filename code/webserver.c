/* James M. Rogers */
/* Copyright 2012  */

#include <unistd.h>

#include "server.h"
#include "threads.h"
#include "pyscript_init.h"


void
NewConn ( int fd )
{ /* Setup datastructure for a new connection on fd. */ }

void
NewData (int fd, char *data, int size)
{ ThreadHandleResponse(fd); }

void
ConnClose (int fd)
{ /* Mark connection as closed. */ }

int
main () {
  Server *New;

  ThreadInitialize(25);
  PythonInit("python");

  New = ServerNew ();
  SetPort (New, 8080, 25);
  SetCallbacks (New, NewConn, NewData, ConnClose);
  ServerOpen (New);
  while(ServerRunning(New)){
    usleep(5);
    ServerLoop (New);
    ThreadCheck();
  }
  ServerClose (New);
  ServerDel (New);
  return 0;
}
