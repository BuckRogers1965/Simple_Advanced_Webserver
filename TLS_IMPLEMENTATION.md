# Adding Secure Connections (TLS)

How HTTPS support was added to the web server, and why it was built the
way it was.

## The goal

The server already spoke HTTP over a raw TCP socket. We wanted it to also
speak HTTPS, without turning the rest of the code into a tangle of
"if secure do this, else do that" branches.

## The key insight: abstract the connection, not the port

The instinct is to think of this as "a second, secure port." That is only
half the story. The real problem lived deeper: everywhere in the code a
bare socket file descriptor (`int fd`) was passed around, and everyone
called the socket syscalls on it directly:

- the worker thread did `recv(fd, ...)`
- the response code did `send(fd, ...)` and `sendfile(fd, ...)`
- the Python/CGI code did `send(fd, ...)`

That works for plaintext. It does **not** work for TLS, because with
OpenSSL you no longer read and write the socket directly — every byte has
to travel through an `SSL` handle (`SSL_read` / `SSL_write`). The bare
`int fd` was the leak. So the thing that had to be abstracted was not the
port, it was **how you read and write a connection.**

The port only decides *which kind* of connection to build. Once the
connection object exists, nothing downstream needs to know or care whether
it is encrypted.

## The `Conn` object

We introduced an opaque connection object, `Conn`, in the project's
existing object-oriented-C style (a `void *` handle with a small set of
methods; the real struct is hidden in the `.c` file).

Files: `code/conn.h`, `code/conn.c`

```c
Conn *ConnNew (int fd, void *ssl_ctx);   /* ssl_ctx == NULL => plaintext */
int   ConnAccept  (Conn *);              /* TLS handshake (no-op if plain) */
int   ConnRead    (Conn *, char *buf, int n);
int   ConnWrite   (Conn *, const char *buf, int n);
int   ConnSendFile(Conn *, int filefd, off_t size);
void  ConnClose   (Conn *);
```

Internally a `Conn` is just:

```c
typedef struct conn {
  int  fd;
  SSL *ssl;   /* NULL means this is a plaintext connection */
} conn;
```

Every method checks `ssl`:

| Method        | Plaintext          | TLS                          |
|---------------|--------------------|------------------------------|
| `ConnAccept`  | returns 1 (no-op)  | `SSL_accept` (handshake)     |
| `ConnRead`    | `recv`             | `SSL_read`                   |
| `ConnWrite`   | `send`             | `SSL_write`                  |
| `ConnSendFile`| `sendfile`         | `read` + `SSL_write` loop    |
| `ConnClose`   | `close`            | `SSL_shutdown` + `SSL_free` + `close` |

That one branch, buried inside `conn.c`, is the *only* place in the whole
server that knows the difference between plaintext and TLS. Everything
else just holds a `Conn` and calls methods on it.

## Two listeners, one accept path

The server object (`code/server.c`) grew a few fields:

- `ssl_ctx` — the OpenSSL context, `NULL` until `SetTLS()` is called
- `TLSSocket` / `TLSPort` — the second listener
- `conns[]` — a map from a client fd to its `Conn *`

`SetTLS(server, port, cert, key)` builds the SSL context from the PEM
files and remembers the secure port. `ServerOpen()` opens the plaintext
listener as before, and — only if a certificate was loaded — opens the
TLS listener too and adds both to the same `epoll` set.

In the event loop (`ServerLoop`), when a listener socket is ready we
figure out *which* listener fired. That decides whether the accepted fd
gets wrapped as plaintext or TLS:

```c
void *ctx = (active == obj->TLSSocket) ? obj->ssl_ctx : NULL;
clifd = accept(active, NULL, NULL);
obj->conns[clifd] = ConnNew(clifd, ctx);   /* plaintext or TLS, decided here */
```

From this point on the fd's "secure or not" nature is baked into its
`Conn`, and the rest of the loop is identical for both. When a client has
data, we hand its **`Conn`** (not a raw fd) to a worker thread and remove
it from `epoll`.

## The threading change

This was the part that was fiddly to get right. A TLS handshake
(`SSL_accept`) is not a single read — it is several round trips. Doing
that on the main `epoll` loop would stall every other connection while one
client negotiates.

The fix: keep the handshake in the worker thread, where blocking is fine
because each worker only handles one connection at a time. The worker
(`code/threads.c`) now owns the whole life cycle of the connection:

```c
if (ConnAccept(t->conn))            /* handshake (no-op for plaintext) */
{
    n = ConnRead(t->conn, t->buffer, 1023);
    if (n > 0)
        HandleResponse(t->conn, t->buffer, n);
}
ConnClose(t->conn);                 /* TLS shutdown + close + free      */
t->conn = NULL;
```

The thread pool itself — the free-list / work-list hand-off — did not
change in shape. The only difference is that a worker slot now carries a
`Conn *` instead of an `int fd`, and it calls `ConnAccept` / `ConnRead` /
`ConnClose` instead of the raw syscalls. Ownership is clean: the main loop
creates the `Conn` and hands it off; the worker uses it and frees it.

## The `sendfile` wrinkle

Static files were served with `sendfile()` — a kernel zero-copy path that
streams a file straight to a socket without the bytes ever entering user
space. That is fast, but it is fundamentally incompatible with userspace
TLS: OpenSSL has to encrypt every byte in user space before it hits the
wire, and you cannot encrypt bytes you never see.

So `ConnSendFile` splits by connection type:

- **Plaintext:** still uses `sendfile()` — nothing lost.
- **TLS:** falls back to the classic loop — `read()` a chunk of the file
  into a buffer, `SSL_write()` it, repeat. No zero-copy, but correct.

There is a fast path for the future: **kernel TLS (kTLS)**. If the kernel
encrypts the TLS records, `sendfile()` works again even over HTTPS. We set
`SSL_OP_ENABLE_KTLS` on the context so the `SSL_write` calls are offloaded
where the kernel supports it. (Turning `ConnSendFile` back into a true
`sendfile()` over kTLS is a possible next step.)

## The Python / CGI path was already fine

The Python scripting path (`code/pyscript.c`) forks a `python3` process,
pipes the request body to its stdin, and reads its stdout back. The parent
already read that output into a buffer and wrote it to the socket — so it
passes through user space anyway. Converting it to TLS was a one-line
change: `send(fd, ...)` became `ConnWrite(conn, ...)`. And because the
child process never touches the socket directly, TLS stays intact.

## Summary of changes

| File | Change |
|------|--------|
| `code/conn.h`, `code/conn.c` | **New.** The `Conn` abstraction + OpenSSL setup. |
| `code/server.h` | Data callback now takes a `Conn *`; added `SetTLS`. |
| `code/server.c` | SSL context, second TLS listener, fd→`Conn` map, accept wraps each fd in a `Conn`. |
| `code/threads.c/.h` | Worker carries a `Conn *`; does `ConnAccept` (handshake) → `ConnRead` → `ConnClose`. |
| `code/response.c/.h` | `HandleResponse(Conn *, ...)` uses `ConnWrite` / `ConnSendFile`. |
| `code/pyscript.c/.h` | `ExecutePythonScript(Conn *, ...)` streams output via `ConnWrite`. |
| `code/webserver.c` | `ConnLibInit()`, enable TLS on 8443, plaintext on 8083. |
| `code/http_request.h` | Added an include guard (now included from two places). |
| `code/genkeys.sh` | **New.** Generates the self-signed `cert.pem` / `key.pem`. |
| `README.md`, `.gitignore` | Build/cert instructions; ignore `*.pem`. |

## Build and run

```sh
sudo apt-get install python3-dev libssl-dev

cd code
sh genkeys.sh          # writes cert.pem and key.pem into this folder

gcc *.c -I/usr/include/python3.11 -lpython3.11 -lssl -lcrypto -ggdb -o webserver
./webserver
```

Open the firewall for both ports:

```sh
sudo ufw allow 8083/tcp
sudo ufw allow 8443/tcp
```

- Plaintext:  `http://<host>:8083/path/`
- Secure:     `https://<host>:8443/path/`

If `cert.pem` / `key.pem` are missing, the server logs a notice and serves
plaintext only.

## Expected behavior with a self-signed certificate

Browsers do not trust a self-signed certificate, so the first time you
visit the HTTPS port you get a "connection may not be private" warning;
clicking through ("proceed anyway") works. On the server side you may see:

```
... sslv3 alert certificate unknown ... SSL alert number 46
```

That is **normal**. Alert 46 (`certificate_unknown`) is the browser
telling the server it does not trust the cert and aborting that first
handshake attempt; when you accept the warning, the browser reconnects and
the handshake completes. It is cosmetic log noise, not a failure. (Modern
browsers also ignore the certificate's `CN` and want a Subject Alternative
Name — adding a SAN to `genkeys.sh` reduces the warnings, though a
self-signed cert will still warn until it is imported into the trust
store.)
