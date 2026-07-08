/* James M. Rogers */
/* Copyright 2012  */

#include <unistd.h>
#include <stdio.h>

#include "server.h"
#include "threads.h"
#include "pyscript_init.h"
#include "conn.h"


void
NewConn ( int fd )
{ /* Setup datastructure for a new connection on fd. */ }

void
NewData (Conn *conn)
{ ThreadHandleResponse(conn); }

void
OnConnClose (int fd)
{ /* Mark connection as closed. */ }

int
main () {
  Server *New;

  ThreadInitialize(25);
  PythonInit("python");
  ConnLibInit();

  New = ServerNew ();
  SetPort (New, 8083, 25);

  /* Enable HTTPS on 8443.  Generate cert.pem / key.pem with the openssl
     command in the README.  If the files are missing we just log it and
     keep serving plaintext on 8080. */
  if (SetTLS (New, 8443, "cert.pem", "key.pem") != 0)
    printf ("TLS disabled: could not load cert.pem/key.pem\n");

  SetCallbacks (New, NewConn, NewData, OnConnClose);
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
