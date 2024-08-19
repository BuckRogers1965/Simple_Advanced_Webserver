/* James M. Rogers */
/* Copyright 2012  */

#include "server.h"
#include "connect.h"
#include "threads.h"
#include <unistd.h>

void
NewConn (int fd)
{
  /* Setup datastructure for a new connection on fd. */
  /* Clear data structures before use. */
  
}


void
NewData (int fd, char *data, int size)
{

  ThreadHandleResponse(fd);
}


void
ConnClose (int fd)
{
  /* Mark connection as closed. */
}

int
main ()
{
  Server *New;

  ThreadInitialize(10, "python");

  New = ServerNew ();
  SetPort (New, 8080, 25);
  SetCallbacks (New, NewConn, NewData, ConnClose);
  ServerOpen (New);
  while(ServerRunning(New)){
    usleep(10);
    ServerLoop (New);
    ThreadCheck();
  }
  ServerClose (New);
  ServerDel (New);
  return 0;
}
