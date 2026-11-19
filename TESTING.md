# Testing

## Milestone 1 checklist

- [ ] Linux `make` build with C++20, warnings, and pthreads.
- [x] POSIX/macOS `make` build with C++20, warnings, and pthreads.
- [x] Valid configuration starts the listener.
- [x] Greeting is `220`.
- [x] `USER anonymous` returns `230`.
- [x] Invalid `USER` returns `530` and closes.
- [x] Commands are case-insensitive.
- [x] `PWD`, `CWD`, and `CDUP` navigate within `db/`.
- [x] Invalid directories return `550`.
- [x] `QUIT` returns `221` and closes.
- [x] Unauthenticated commands return `530`.
- [x] Multiple clients remain independent.

## Phase 3 checklist: passive mode and local transfers

- [x] `PASV` opens an ephemeral IPv4 listener and returns a valid `227` endpoint.
- [x] `LIST` without a data connection returns `425`.
- [x] `LIST [path]` returns directory entries over one data connection.
- [x] Successful `LIST` uses `150` followed by `226`.
- [x] `RETR <path>` returns local file bytes without modification.
- [x] Successful `RETR` uses `150` followed by `226`.
- [x] Data connections close after one transfer.
- [x] Invalid `LIST` and `RETR` paths return `550`.
- [x] `RETR` without a data connection returns `425`.
- [x] Phase 3 paths continue enforcing the database jail.

## Phase 4 checklist: filesystem security and directory jail

- [x] `CWD` rejects parent-directory traversal outside `db/`.
- [x] `CWD`, `LIST`, and `RETR` reject absolute paths outside `db/`.
- [x] Existing paths are resolved canonically before access.
- [x] Symlinks resolving outside `db/` are rejected.
- [x] NUL-containing path arguments are rejected safely.
- [x] Valid root and nested paths remain accessible.

## Phase 5 checklist: P2P listener and peer authentication

- [x] P2P listener binds to `PORT + 200`.
- [x] Valid `USER peer@<actual-source-IP>` returns `230 Peer authenticated, proceed.`.
- [x] Incorrect claimed peer IP returns `530` and closes the connection.
- [x] Non-peer authentication returns `530` and closes the connection.
- [x] Peer commands before authentication are rejected.
- [x] Peer command handling is gated behind successful authentication.
- [x] Peer sessions run concurrently with each other and FTP clients.
- [x] CIDR parsing, bounded scanning, periodic scheduling, and peer snapshot replacement are implemented.
- [ ] Successful multi-host discovery and synchronization on the school network.

## Phase 7 checklist: distributed LIST

- [x] Local and synchronized peer entries are merged under the requested directory.
- [x] Peer-only entries are labeled `peer`.
- [x] Local entries take precedence over same-name peer entries.
- [x] Listing output is deterministic by entry name.
- [x] Solo-mode `LIST` remains local-only.
- [ ] End-to-end distributed `LIST` with a distinct reachable peer.

## Phase 8 checklist: distributed RETR

- [x] Local `RETR` remains byte-for-byte correct.
- [x] Authenticated peer-side `RETR` streams local peer files.
- [x] Contact-server relay retrieves a peer-owned file through the client data socket.
- [x] Relay uses `150` and `226` control responses.
- [ ] Direct PASV redirection to a distinct zone peer.

## Phase 9 checklist: concurrency and cleanup

- [x] Multiple FTP sessions run concurrently.
- [x] Abrupt FTP disconnects do not terminate the server.
- [x] Abrupt peer disconnects do not terminate the server.
- [x] Passive listeners and transfer sockets are closed after success or failure.
- [x] Peer scan failures remove stale peer snapshots.

## Phase 10 checklist: final validation

- [x] Warning-enabled clean build.
- [x] Functional success and failure tests recorded separately.
- [x] Documentation updated with unavailable zone-only tests.
- [ ] Linux VM and SIUE zone-server validation.

## Execution record

2026-11-18, macOS/Darwin:

- Verified repository branch and commit: `main` at `8616528`.
- Ran `make` in `starter-code/`.
  - Command: `g++ -std=c++20 -Wall -Wextra -Wpedantic -pthread server.cpp -o server`
  - Result: passed with no compiler warnings printed.
- Confirmed this environment is not Linux: `uname -a` reported Darwin.
- Attempted to start the checked-in `server.conf` on port `30000`.
  - Result inside sandbox: `Unable to bind port 30000`.
  - Result outside sandbox: `FTP server listening on port 30000`.
- Started the server from the repository root with a temporary test config on port `40047`, so `./db` was available as the jail root.
  - Result: `FTP server listening on port 40047`.
  - Local TCP bind/connect required sandbox approval in this environment.
- Ran socket integration tests with a Python localhost client:
  - Greeting returned `220 Distributed FTP Server Ready`.
  - `USER anonymous` returned `230 User logged in, proceed.`.
  - Invalid `USER kevin` returned `530 Authentication failed.` and the connection closed.
  - Mixed-case commands `user`, `pWd`, `cWd`, `CdUp`, and `qUiT` worked.
  - `PWD`, `CWD comp.tech`, `CDUP`, and repeated `PWD` showed correct virtual paths.
  - `CWD ..`, `CWD /../../`, and `CWD does-not-exist` returned `550 Failed to change directory.`.
  - `PWD` before login returned `530 Not logged in.`.
  - Deferred commands `PASV`, `LIST`, and `RETR comp.tech/537.txt` returned `502 Command not implemented in Milestone 1.`.
  - Three simultaneous clients each received independent greeting, login, `PWD`, and `QUIT` responses.
- Ran `make clean` after testing to remove the generated `server` binary.
- Rebuilt once more for the checked-in `server.conf` smoke start, then ran `make clean` again to remove the generated `server` binary.

Milestone 1 still needs the same `make` and socket test pass on an actual Linux host before it should be considered Linux verified.

2026-11-18, Phase 3 macOS/Darwin integration:

- Implemented `PASV`, local `LIST`, and local binary `RETR`.
- `make` passed with `-std=c++20 -Wall -Wextra -Wpedantic -pthread` and no warnings printed after fixes.
- PASV parsing and data-channel connection succeeded on `127.0.0.1`.
- `LIST comp.security` returned expected entries and `150`/`226` responses.
- Local `RETR comp.security/70662.txt` matched the source file byte-for-byte for repeated transfers, including mixed-case `retr`.
- Missing data connections returned `425`.
- Invalid `LIST` and `RETR` paths returned `550`.
- Existing authentication was corrected so `USER ANONYMOUS` is accepted case-insensitively.
- Tests were run against the server launched from the repository root with the checked-in configuration.

2026-11-18, Phase 4 security integration:

- `make` passed with `-std=c++20 -Wall -Wextra -Wpedantic -pthread` and no warnings printed.
- Hardened path resolution to use canonical paths and component-aware root containment.
- Rejected `CWD ..`, `CWD /tmp`, `CWD /../../`, and a NUL-containing path with `550`.
- Rejected temporary symlinks from `db/` to `/tmp` and `/etc/passwd` for `CWD`, `LIST`, and `RETR`.
- Confirmed valid root `LIST` and local `RETR` still succeeded after hardening.
- Temporary symlinks were removed after testing.

2026-11-18, Phase 5 P2P integration:

- `make` passed with `-std=c++20 -Wall -Wextra -Wpedantic -pthread` and no warnings printed.
- Checked-in configuration started the FTP listener on port `30000` and the P2P listener on port `30200`.
- `USER peer@127.0.0.1` authenticated using the actual incoming socket source address.
- `USER peer@127.0.0.2`, `USER anonymous`, and pre-auth `LIST` were rejected with `530` and connection closure.
- Authenticated peer commands were gated and returned the Phase 5 deferred-command response.
- Two peer sessions authenticated concurrently while the FTP listener remained available.

2026-11-18, Phase 6 discovery and synchronization:

- Implemented periodic `PEER_SUBNET` scanning on `PORT + 200`.
- Implemented authenticated peer `PASV`/`LIST` exchange and recursive remote file inventory collection.
- Peer snapshots are replaced after successful scans and removed after failed connections.
- Peer LIST protocol tests passed for root and nested directory listings.
- Started a temporary `127.0.0.0/30` scan configuration successfully.
- A successful distinct-peer scan was unavailable on this macOS host: `127.0.0.1` is the local endpoint and `127.0.0.2` was not reachable as a distinct loopback peer. Multi-host discovery remains to be verified on the school network.

2026-11-18, Phase 7 distributed LIST:

- Implemented synchronized snapshot merging in the shared LIST generator.
- In-process snapshot test passed for peer-only entries, peer labeling, deterministic ordering, and local collision precedence.
- Solo-mode network LIST regression passed and remained local-only.
- End-to-end multi-host distributed LIST remains unavailable until a distinct reachable peer can be tested on the school network.
