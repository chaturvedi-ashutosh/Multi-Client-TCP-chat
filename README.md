# TCP Customer Support Chat

A small C++17 customer support chat over TCP. Customers connect to the server and wait in a FIFO queue. One technician requests the next customer; the server routes messages only between that active pair.

## Requirements

- Ubuntu or another Linux system with POSIX sockets
- g++ with C++17 support
- GNU Make
- No third-party libraries

## Build

From the project directory:

```bash
make
```

To remove the generated programs:

```bash
make clean
```

## Run

Run the server in one terminal:

```bash
./server 5555
```

Connect one or more customers in separate terminals:

```bash
./client 127.0.0.1 5555 Alice
./client 127.0.0.1 5555 Bob
```

Connect the technician in another terminal:

```bash
./technician 127.0.0.1 5555 Ava
```

The customer name and technician name are optional; the defaults are `Customer` and `Technician`.

### Customer commands

- `/status` shows whether the customer is waiting or in an active session.
- `/quit` disconnects. During an active session, this notifies the technician and clears the session.
- Other input is sent to the technician only while assigned.

### Technician commands

- `next` requests the next customer in queue order.
- `end` ends the active session and places that customer at the back of the queue.
- `/status` shows whether a customer session is active.
- `/quit` disconnects. If a session is active, its customer returns to the front of the queue.
- Other input is sent to the customer only while a session is active.

## Protocol

Messages are newline-delimited text. Lines may contain up to 1024 bytes, excluding the line ending. A trailing carriage return before newline is ignored. `|` separates command fields; message text may also contain `|`.

| Direction | Command | Meaning |
| --- | --- | --- |
| Client → server | `LOGIN|CUSTOMER|<name>` | Register and queue a customer |
| Client → server | `LOGIN|TECHNICIAN|<name>` | Register the single technician |
| Technician → server | `REQUEST_NEXT` | Assign the next waiting customer |
| Active peer → server | `MESSAGE|<text>` | Send a private session message |
| Technician → server | `END_SESSION` | End the session and requeue the customer |
| Either client → server | `DISCONNECT` | Leave the queue or session |
| Server → customer | `WAITING|<customer_id>` | Customer is waiting |
| Server → both peers | `ASSIGN|<customer_id>|<name>` | Session assignment |
| Server → technician | `TECHNICIAN_READY`, `QUEUE_EMPTY`, `BUSY` | Technician status |
| Server → active peer | `MESSAGE|<text>` | Message from the other session member |
| Server → customer | `WAITING|SESSION_ENDED` | Session ended; customer rejoined the queue |
| Server → customer | `WAITING|TECHNICIAN_DISCONNECTED` | Technician dropped; customer returned to the front |
| Server → customer | `WAITING|ASSIGNMENT_FAILED` | Assignment failed; customer was returned to queue |
| Server → technician | `SESSION_ENDED|CUSTOMER_DISCONNECTED` | Customer disconnected during the session |
| Server → client | `ERROR|<reason>` | Invalid command or state |

Names must be 1–64 bytes and cannot contain `|`, carriage return, or newline. Message text cannot contain newline. The protocol helpers and line limit are defined in `src/chat.hpp`.

## Design notes

- The main server thread owns the listening socket and accepts connections.
- Each accepted connection gets one detached handler thread. That handler owns and closes the accepted socket. Detached threads let the server continue accepting connections; stopping the process does not wait for handlers to join.
- One mutex protects the customer map, FIFO queue, technician fd, active customer fd, and customer ID counter. The server copies or duplicates destination descriptors while locked, releases the mutex, and then performs blocking network I/O.
- A temporary duplicated fd keeps a recipient socket from being confused with a later connection if the original fd closes and its number is reused.
- `send_all()` handles partial writes and uses `MSG_NOSIGNAL`. `LineReader` buffers partial and coalesced input, retries interrupted receives, and enforces the line limit.
- The server permits one technician at a time. Empty queues are reported with `QUEUE_EMPTY`.

## Manual checks

Run each process in its own terminal. Check that multiple customers stay connected, assignments follow arrival order, messages reach only the active pair, `end` permits the next assignment, and disconnecting a waiting or active customer cleans up the state. Also disconnect the technician during a session and confirm the customer returns to the front of the queue. Repeated connect/disconnect cycles should leave the server running.

## Git setup

Generated executables are ignored by `.gitignore`. If this directory is not already a Git repository, initialize it from the project directory:

```bash
git init
git status --short
git add .gitignore AGENTS.md Makefile README.md src
git diff --cached
git commit -m "Build TCP customer support chat"
```

Review `git status` and the staged diff before committing. Use `git status --short` after future edits to see which source and documentation files changed.
