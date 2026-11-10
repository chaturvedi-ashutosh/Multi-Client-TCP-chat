# AGENTS.md: TCP Customer Support Chat (C++ / POSIX sockets)

## Project
Multi-client support chat over TCP. Customers connect to a central server and wait in a queue. One technician pulls the next customer, and the pair chat privately until the session ends.

Interview project: code must stay simple enough for the owner to explain and defend. Prefer correct and simple over clever.

## Stack (fixed)
- C++17, g++, Make, Ubuntu (WSL2). Linux/POSIX sockets only, no Winsock.
- `std::thread`, `std::mutex`, `std::lock_guard`, `std::queue`.
- No third-party libraries. No epoll/async. No DB, auth, encryption, file transfer, or design-pattern abstractions.

## Structure
```
src/server.cpp      server (thread per client)
src/client.cpp      customer client
src/technician.cpp  technician client
src/chat.hpp        shared protocol helpers (send_all, line reader, parsing)
Makefile  README.md  .gitignore
```
Start with `server.cpp` and `client.cpp`. Add files only when needed.

## Architecture
- Main thread: `socket`, `bind`, `listen`, `accept` loop. One thread per connected client.
- Shared state (mutex-protected): connected customers, waiting queue, technician connection, active session (customer paired with technician).
- States: *waiting* (in queue), *assigned* (removed from queue, paired), *active session* (live chat).
- Flow: customer connects, `LOGIN`, enqueued, technician `REQUEST_NEXT`, `ASSIGN` sent to both, private `MESSAGE`s, `END_SESSION`, technician can request the next customer.

## Protocol
Newline-delimited text, `|`-separated fields, max line length enforced. Commands:
```
LOGIN|CUSTOMER|<name>     LOGIN|TECHNICIAN|<name>
REQUEST_NEXT              ASSIGN|<id>|<name>
MESSAGE|<text>            END_SESSION
```
Final format is fixed in the protocol phase. Record it in `chat.hpp` and the README, and do not change it silently.

## Hard rules
1. TCP is a byte stream: use `send_all()` for partial sends and a buffered line reader for partial and coalesced receives.
2. `recv()` returning 0 means the peer closed. Negative means check `errno` (retry on `EINTR`).
3. Use `MSG_NOSIGNAL` on `send()` (or ignore `SIGPIPE`). Set `SO_REUSEADDR` on the listener.
4. **Never hold a mutex during a blocking `send`/`recv`.** Copy what you need under the lock, release, then do I/O.
5. Lock from the moment shared data first appears. If more than one mutex exists, document the lock order.
6. Each fd has one owner that closes it. No fd leaks on any error path.
7. Decide and document thread lifecycle (detach vs join).
8. Only one technician is allowed (reject a second login). If the technician drops mid-session, notify the customer and put them back at the front of the queue.
9. Customers never receive messages meant for others. No duplicate assignment. Handle an empty queue.
10. Check every socket call. No silently ignored errors. Reject malformed messages.

## Working rules
- Build in phases, one at a time: basic server, basic client, two-way chat (receiver thread), multi-client, waiting queue, private routing, graceful disconnection, error handling, CLI polish, review, README. GUI, Docker, and deployment are optional and come only after the core passes all tests.
- Generate real code, then give the exact commands to build and run it, and ask for output or errors. Do not assume success.
- Edit existing code instead of rewriting it. Keep file names, function names, and decisions stable.
- When debugging, read the code and find the actual cause before proposing a fix.
- Briefly explain why each component exists. Mark things as implemented, proposed, or tested.
- If you spot a design flaw, explain it and propose the simplest reliable fix before continuing.
- Keep responses concise.

## Build and run
```
make
./server <port>
./client <host> <port>
./technician <host> <port>
```

## Acceptance tests (done = all pass)
1. One client connects and exchanges messages.
2. Three or more customers stay connected.
3. Queue order is correct.
4. Technician is assigned the correct customer, and the customer is notified.
5. Private messages reach only the assigned pair.
6. After `END_SESSION`, the technician can request the next customer.
7. A waiting customer disconnecting is removed from the queue.
8. An active customer disconnecting cleans up the session.
9. Repeated connect/disconnect leaves the server stable.

## Progress (update after each phase)
```
Current phase: 11 — Complete
Completed: Phases 1–9 were implemented and the user reported they work correctly. Phase 10 code review checked routing, queue state, lock scope, fd ownership, and thread lifecycle. Cross-handler sends now duplicate the destination fd under the state mutex and perform blocking I/O after unlocking, preventing descriptor reuse from redirecting a stale send. Handler startup avoids terminating if detach fails. Phase 11 adds the Makefile, `.gitignore`, full protocol/design/build/run README, manual check list, and Git setup guidance.
Key decisions: One mutex protects queue, customer, technician, and active-session state. Temporary duplicate descriptors are owned by the sending function and closed after each send. Detached handlers own accepted sockets; detach failure is joined so the handler remains the sole fd owner. Generated executables are ignored and kept out of source control.
Open issues: Detached threads mean server shutdown uses process termination rather than a coordinated join. The final build succeeded; the last server-source review change was compiled, while client and technician targets were already up to date.
Tests passed: The user confirmed phases 1–9, including queue order, private routing, disconnect cleanup, errors, and CLI status. `make` completed successfully in Ubuntu with no reported errors.
```
