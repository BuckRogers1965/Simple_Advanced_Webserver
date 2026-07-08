/* James M. Rogers */
/* Copyright 2012  */

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/sendfile.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "conn.h"

/* The concrete connection.  ssl == NULL means a plaintext connection. */
typedef struct conn {
  int fd;
  SSL *ssl;
} conn;

void
ConnLibInit (void) {
  SSL_library_init ();
  SSL_load_error_strings ();
  OpenSSL_add_all_algorithms ();
}

void *
ConnServerCTX (const char *cert, const char *key) {
  SSL_CTX *ctx = SSL_CTX_new (TLS_server_method ());
  if (!ctx)
    {
      ERR_print_errors_fp (stderr);
      return NULL;
    }

  /* Let the kernel do the record encryption when it can.  This is what
     lets sendfile() serve encrypted data at speed (kTLS); it is silently
     ignored on kernels/builds that do not support it. */
#ifdef SSL_OP_ENABLE_KTLS
  SSL_CTX_set_options (ctx, SSL_OP_ENABLE_KTLS);
#endif

  if (SSL_CTX_use_certificate_file (ctx, cert, SSL_FILETYPE_PEM) <= 0)
    {
      ERR_print_errors_fp (stderr);
      SSL_CTX_free (ctx);
      return NULL;
    }

  if (SSL_CTX_use_PrivateKey_file (ctx, key, SSL_FILETYPE_PEM) <= 0)
    {
      ERR_print_errors_fp (stderr);
      SSL_CTX_free (ctx);
      return NULL;
    }

  if (!SSL_CTX_check_private_key (ctx))
    {
      fprintf (stderr, "conn: private key does not match certificate\n");
      SSL_CTX_free (ctx);
      return NULL;
    }

  return ctx;
}

Conn *
ConnNew (int fd, void *ssl_ctx) {
  conn *c = (conn *) calloc (1, sizeof (conn));
  if (!c)
    return NULL;

  c->fd = fd;

  if (ssl_ctx)
    {
      c->ssl = SSL_new ((SSL_CTX *) ssl_ctx);
      if (!c->ssl)
	{
	  ERR_print_errors_fp (stderr);
	  free (c);
	  return NULL;
	}
      SSL_set_fd (c->ssl, fd);
    }

  return (Conn *) c;
}

int
ConnAccept (Conn *v) {
  conn *c = (conn *) v;

  if (!c->ssl)
    return 1;   /* plaintext: nothing to negotiate */

  if (SSL_accept (c->ssl) <= 0)
    {
      ERR_print_errors_fp (stderr);
      return 0;
    }
  return 1;
}

int
ConnRead (Conn *v, char *buf, int n) {
  conn *c = (conn *) v;

  if (c->ssl)
    return SSL_read (c->ssl, buf, n);
  return recv (c->fd, buf, n, 0);
}

int
ConnWrite (Conn *v, const char *buf, int n) {
  conn *c = (conn *) v;

  if (c->ssl)
    return SSL_write (c->ssl, buf, n);
  return send (c->fd, buf, n, 0);
}

int
ConnSendFile (Conn *v, int filefd, off_t size) {
  conn *c = (conn *) v;

  /* Plaintext: kernel zero-copy straight from file to socket. */
  if (!c->ssl)
    {
      off_t off = 0;
      while (off < size)
	{
	  ssize_t s = sendfile (c->fd, filefd, &off, size - off);
	  if (s <= 0)
	    return -1;
	}
      return 0;
    }

  /* TLS: OpenSSL encrypts in userspace, so there is no zero-copy path we
     can use portably - read a chunk and SSL_write it.  (When kTLS is
     active the SSL_write below is still offloaded to the kernel.) */
  {
    char buf[16384];
    ssize_t r;
    while ((r = read (filefd, buf, sizeof (buf))) > 0)
      {
	int written = 0;
	while (written < r)
	  {
	    int w = SSL_write (c->ssl, buf + written, (int) (r - written));
	    if (w <= 0)
	      {
		ERR_print_errors_fp (stderr);
		return -1;
	      }
	    written += w;
	  }
      }
    if (r < 0)
      return -1;
  }
  return 0;
}

int
ConnFd (Conn *v) {
  return ((conn *) v)->fd;
}

void
ConnClose (Conn *v) {
  conn *c = (conn *) v;

  if (!c)
    return;

  if (c->ssl)
    {
      SSL_shutdown (c->ssl);
      SSL_free (c->ssl);
    }
  close (c->fd);
  free (c);
}
