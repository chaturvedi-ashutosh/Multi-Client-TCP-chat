# TCP Customer Support Chat

A small C++17 customer support chat over TCP. Customers connect to the server and wait in a FIFO queue. One technician requests the next customer; the server routes messages only between that active pair.

## Requirements

For the server and console clients:

- Ubuntu or another Linux system with POSIX sockets
- g++ with C++17 support
- GNU Make
- No third-party libraries

For the graphical client:

- Qt 6 Widgets and Qt 6 Network development packages
- `pkg-config`
- A Linux graphical desktop session (WSLg works when running Ubuntu through WSL2)

## Build

From the project directory:

```bash
make
```

To remove the generated programs:

```bash
make clean
```

### Build the graphical client

Install the Qt 6 development packages and `pkg-config` on Ubuntu:

```bash
sudo apt update
sudo apt install g++ make pkg-config qt6-base-dev
```

Build the GUI separately from the existing console programs:

```bash
make gui
```

Run it with:

```bash
./chat_gui
```

The connection window lets you choose a customer or technician role and enter a display name, server address, and port. Each role moves into its own graphical workspace after connecting. The original `server`, `client`, and `technician` console targets remain available.

To try the graphical chat, start the server and open `./chat_gui` in two windows. Connect one as a customer and one as a technician. The customer sees the technician's name after assignment, and the technician workspace shows both the technician and active customer's names. Use **Next customer** to assign the waiting customer, then send private messages. The technician can also send a public announcement to every connected customer. **End session** returns the customer to the queue.

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
- The customer terminal labels status updates, technician messages, public notices, and errors so the session history is easy to scan. Broadcasts appear as `[PUBLIC NOTICE]`.

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
| Technician → server | `BROADCAST|<text>` | Send a notice to every connected customer |
| Technician → server | `END_SESSION` | End the session and requeue the customer |
| Either client → server | `DISCONNECT` | Leave the queue or session |
| Server → customer | `WAITING|<customer_id>` | Customer is waiting |
| Server → customer | `ASSIGN|<customer_id>|<technician_name>` | Session assignment and assigned technician's name |
| Server → technician | `ASSIGN|<customer_id>|<customer_name>` | Session assignment and assigned customer's name |
| Server → technician | `TECHNICIAN_READY`, `QUEUE_EMPTY`, `BUSY` | Technician status |
| Server → active peer | `MESSAGE|<text>` | Message from the other session member |
| Server → technician | `BROADCAST_SENT|<customer_count>` | Broadcast was sent to the connected customers |
| Server → customer | `BROADCAST|<text>` | Public notice from the technician |
| Server → customer | `WAITING|SESSION_ENDED` | Session ended; customer rejoined the queue |
| Server → customer | `WAITING|TECHNICIAN_DISCONNECTED` | Technician dropped; customer returned to the front |
| Server → customer | `WAITING|ASSIGNMENT_FAILED` | Assignment failed; customer was returned to queue |
| Server → technician | `SESSION_ENDED|CUSTOMER_DISCONNECTED` | Customer disconnected during the session |
| Server → client | `ERROR|<reason>` | Invalid command or state |

Names must be 1–64 bytes and cannot contain `|`, carriage return, or newline. Message text cannot contain newline. The protocol helpers and line limit are defined in `src/chat.hpp`.

## Design notes

- The main server thread owns the listening socket and accepts connections.
- Each accepted connection gets one detached handler thread. That handler owns and closes the accepted socket. Detached threads let the server continue accepting connections; stopping the process does not wait for handlers to join.
- One mutex protects the customer map, FIFO queue, technician fd and name, active customer fd, and customer ID counter. The server copies or duplicates destination descriptors while locked, releases the mutex, and then performs blocking network I/O.
- A temporary duplicated fd keeps a recipient socket from being confused with a later connection if the original fd closes and its number is reused.
- `send_all()` handles partial writes and uses `MSG_NOSIGNAL`. `LineReader` buffers partial and coalesced input, retries interrupted receives, and enforces the line limit.
- The server permits one technician at a time. Empty queues are reported with `QUEUE_EMPTY`.
- A technician broadcast is sent to all customers registered as connected when the server handles the command, including waiting customers and the active customer. It does not go to the technician. `MESSAGE|<text>` remains private to the active pair.
- The server supports `BROADCAST|<text>`. The graphical technician workspace exposes broadcasts; the technician command-line client does not.
- `ASSIGN` keeps the same three-field format but gives each role the other participant's name: the customer receives the technician name, and the technician receives the customer name.
- The Qt GUI uses Qt Widgets for its windows and Qt Network's `QTcpSocket` for TCP. It reads newline-delimited protocol messages from the socket's event signals. The application owns the socket so the connection window can close while the customer or technician workspace stays connected.
- `make` builds the original console programs. `make gui` separately builds `chat_gui` and requires Qt 6 Widgets, Qt 6 Network, and `pkg-config`.

## Manual checks

Run the server and open separate customer and technician GUI windows. Check that choosing a role closes the drop-down and that connecting opens the role's workspace while closing the connection window. Assign the waiting customer and confirm each side sees the other participant's name. Exchange private messages; verify another waiting customer's window does not receive them. Connect a second customer, send a broadcast, and confirm both the waiting and active customers see it. End the session and verify the customer returns to the queue and the technician can request the next one. Also close a waiting customer, an active customer, and the technician during a session to check queue/session cleanup. Repeated connect/disconnect cycles should leave the server running.

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
