/* James M. Rogers */
/* Copyright 2012  */

/*
 * Conn - an opaque connection object.
 *
 * The rest of the server used to pass a raw socket fd around and call
 * recv()/send()/sendfile() on it directly.  That works for plaintext but
 * not for TLS, where every byte has to travel through an SSL handle.
 *
 * Conn hides that difference behind a small method set.  A plaintext Conn
 * wraps the socket syscalls; a TLS Conn wraps the OpenSSL calls.  Callers
 * (threads.c, response.c, pyscript.c) just talk to a Conn and never know
 * which kind they hold.
 */

#ifndef CONN_H
#define CONN_H

#include <sys/types.h>

typedef void Conn;   /* opaque - see struct conn in conn.c */

/* One-time process wide OpenSSL initialisation. */
void ConnLibInit (void);

/* Build a server SSL context from a PEM certificate and private key.
   Returns an SSL_CTX* (as void* so callers need no OpenSSL headers),
   or NULL on failure. */
void *ConnServerCTX (const char *cert, const char *key);

/* Wrap an accepted socket fd.  ssl_ctx == NULL gives a plaintext Conn,
   otherwise a TLS Conn using that context. */
Conn *ConnNew (int fd, void *ssl_ctx);

/* Complete the TLS handshake (no-op for plaintext).  Returns 1 on
   success, 0 on failure.  Call once before the first ConnRead. */
int ConnAccept (Conn *);

int ConnRead (Conn *, char *buf, int n);
int ConnWrite (Conn *, const char *buf, int n);

/* Send the first `size` bytes of an open file to the connection.
   Plaintext uses the kernel zero-copy sendfile(); TLS falls back to a
   read()+SSL_write() loop.  Returns 0 on success, -1 on error. */
int ConnSendFile (Conn *, int filefd, off_t size);

int ConnFd (Conn *);

/* Shut down TLS if present, close the socket, and free the Conn. */
void ConnClose (Conn *);

#endif /* CONN_H */
