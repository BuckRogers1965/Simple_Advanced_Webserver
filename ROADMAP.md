# Roadmap

Where the web server goes next, in phases. Each phase delivers something a
user can actually use, and every item is weighed against the project's
first rule: **keep the code simple and cleanly modular** (see `GEMINI.md`).

## Where we are

The core is small but genuinely fast, and that is not an accident — it is
the architecture:

- **`epoll`** manages thousands of connections on one thread with almost
  no overhead.
- **A pre-created thread pool** (free-list / work-list) does the actual
  request work, so accepting and processing are decoupled.
- **`sendfile()`** streams static files straight from the kernel to the
  socket — the bytes never enter user space.
- **TLS** now rides the same design through the `Conn` object, without the
  rest of the code knowing.

So for static files the hot path barely touches user space. The design
supports high throughput.

The single biggest thing capping it today is that **each connection serves
exactly one request and then closes** — every request pays for a full TCP
(and TLS!) setup and teardown. That is why HTTP keep-alive (Phase 3) is the
highest-leverage performance item on this list.

## Guiding principles (so we don't lose the plot)

1. One concern per module, opaque objects, small method sets — the style
   already used by `server`, `threads`, `conn`, `http_request`.
2. Prefer a plain array or linked list over a clever data structure.
3. New behavior should be a new small module or a new method on an
   existing object, not an `if` branch bolted into the hot path.
4. **Explicitly out of scope** (would trade away the simplicity that is
   the point of this project): HTTP/2, an async state-machine rewrite,
   moving TLS handshakes into the epoll loop, and any large framework or
   dependency.

---

## Phase 1 — Correct, complete HTTP responses

*Foundation. Low complexity, high payoff — everything later builds on it.*

Today a `200` reply is literally `HTTP/1.1 200 OK\r\n\r\n` followed by the
raw file. There is no `Content-Type`, so browsers guess; there is no
`Content-Length`, which blocks keep-alive later.

Features:
- **MIME types by file extension** — a small static table (`.html`,
  `.css`, `.js`, `.png`, `.jpg`, `.json`, …). One lookup function.
- **Real response headers** — `Content-Type`, `Content-Length` (from the
  `fstat` size we already have), `Date`, `Server`, `Connection: close`.
- **`HEAD` support** and a clean `405` for methods we don't handle.
- **Read the whole request** — the worker currently does one `recv` of up
  to 1023 bytes; read until the headers are complete so large requests
  don't get truncated.

New module: `response_headers.c/.h` (or keep it inside `response.c`).
Touches: `response.c`, `threads.c` (the read loop).

## Phase 2 — Configurability

*Turns it from "edit the source and recompile" into a real tool. This is
the author's own TODO list.*

Features:
- **Command-line arguments**: HTTP port, TLS port, thread count, document
  root, python root. (`./webserver --port 8083 --root ./www --threads 25`)
- **A tiny config file** — simple `key = value` lines, parsed into the
  same settings. No format libraries; a dozen lines of `fgets`.
- **Stop hardcoding the python path** and **drop the required `path/`
  prefix** in URLs.
- **Fix the reversed query-parameter order** (the list is built by
  prepending; append instead, or reverse once after parsing).

Touches: `webserver.c` (arg parsing → `SetPort`/`SetTLS`/etc.), a new
`config.c/.h`, `http_request.c` (param order), `response.c`/`pyscript.c`
(roots).

## Phase 3 — Keep-alive & connection efficiency

*The big performance step. Needs Phase 1's `Content-Length` framing.*

Features:
- **HTTP keep-alive** — after sending a response, if the client asked to
  keep the connection open, loop in the worker and read the next request
  on the same `Conn` instead of closing. Honor `Connection: close` and a
  request cap / idle timeout so connections don't leak.
- **Quieter thread pool** — replace the `usleep` / `sched_yield` spin-wait
  with a condition variable so idle workers don't burn CPU. Same free-list
  / work-list shape, just a cleaner wait.

This is where the existing architecture really pays off: eliminating
per-request setup is pure throughput, and the `Conn` object already gives
us one clean place to loop.

Touches: `threads.c` (worker loop + wait), `response.c` (report whether to
keep the connection).

## Phase 4 — Richer static file serving

*Plays directly to the `sendfile` strength.*

Features:
- **Range requests** (`Accept-Ranges` / `206 Partial Content`) — lets
  browsers seek in video/audio and resume downloads. `sendfile` already
  takes an offset and length, so this is mostly header parsing.
- **Directory autoindex** — when a directory has no `index.html`, generate
  a simple listing instead of a 404.
- **Custom error pages** — serve `404.html` / `50x.html` from the doc root
  if present.

Touches: `response.c`, `conn.c` (range-aware send, small addition).

## Phase 5 — Observability & operations

*Makes it something you can actually run and watch.*

Features:
- **Access logging** — one line per request in Common Log Format
  (client, method, path, status, bytes). A small `log.c/.h`.
- **Debug verbosity levels** — replace the scattered `printf("."/"+"/"-")`
  with a leveled logger (`-v`, `-vv`).
- **Graceful shutdown** — handle `SIGINT`/`SIGTERM`: stop accepting, let
  workers drain, close the listeners (today `ServerClose` is a stub that
  doesn't actually close the sockets).

Touches: new `log.c/.h`, `server.c` (signal handling + real
`ServerClose`), light edits everywhere the debug chars are printed.

## Phase 6 — Extensibility & routing *(optional, advanced)*

*Leans into the callback/object design the project already has.*

Features:
- **A small route table** mapping a path prefix to a handler (static,
  python, redirect). Static-vs-python stops being a hardcoded `strstr` in
  `HandleResponse` and becomes two registered handlers — same behavior,
  cleaner shape, easy to extend.
- **HTTP → HTTPS redirect** — the plaintext listener answers with a `301`
  to the `https://` URL. A natural companion to the TLS work.
- **SAN in `genkeys.sh`** — add a Subject Alternative Name so browsers
  complain less about the self-signed cert.

Touches: a new `router.c/.h`, `response.c` (dispatch through the router),
`genkeys.sh`.

---

## Suggested order

Phases 1 → 2 → 3 give the most value fastest: correct responses, real
configurability, then the keep-alive throughput jump. Phases 4–6 are
independent and can be picked up in any order based on what a user needs
(media serving → 4, running it for real → 5, plugging in new endpoints →
6).

Each phase is self-contained: it should compile, run, and be demoable on
its own before the next one starts.
