# Progress

## Milestone 1

- Status: In progress
- Implemented: configuration parsing, IPv4 TCP listener, concurrent client threads, anonymous authentication, `PWD`, `CWD`, `CDUP`, `QUIT`, CRLF replies, and initial path containment.
- Verified on macOS/Darwin: `make` completed with `-std=c++20 -Wall -Wextra -Wpedantic -pthread` and no warnings printed; localhost socket integration tests passed for greeting, authentication success/failure, unauthenticated rejection, case-insensitive commands, directory navigation, jail containment, `QUIT`, deferred Milestone 2 commands, and concurrent clients.
- Remaining: repeat compilation and socket integration tests on an actual Linux host.
- Deferred by milestone: `PASV`, `LIST`, `RETR`, and P2P behavior.

## Phase 3 — Passive mode and local transfers

- Status: Complete on macOS/Darwin; Linux verification remains outstanding.
- Implemented: ephemeral IPv4 `PASV`, one-shot data connections, local directory listings, binary local `RETR`, transfer response sequencing, and transfer-path validation.
- Verified: successful and failing `LIST`/`RETR` cases, PASV endpoint parsing, repeated transfers, byte-for-byte retrieval, missing data connection handling, and invalid-path handling.
- Compatibility fix: `USER anonymous` comparison is now case-insensitive for `USER ANONYMOUS` and equivalent forms.
- Remaining: repeat the Phase 3 build and integration tests on an actual Linux host.
- Deferred: filesystem hardening beyond the current jail checks, P2P listener/discovery, distributed `LIST`, and distributed `RETR`.

## Phase 4 — Filesystem security and directory jail

- Status: Complete on macOS/Darwin; Linux verification remains outstanding.
- Implemented: centralized canonical path resolution, component-aware containment checks, safe handling of empty paths, and rejection of NUL-containing arguments.
- Verified: traversal attempts, outside absolute paths, outside symlink targets, valid nested navigation, root `LIST`, and local `RETR`.
- Remaining: repeat the Phase 4 build and security integration tests on an actual Linux host.
- Deferred: P2P listener/discovery, distributed `LIST`, and distributed `RETR`.

## Phase 5 — P2P listener and peer authentication

- Status: Complete on macOS/Darwin; Linux verification remains outstanding.
- Implemented: dedicated P2P listener on `PORT + 200`, concurrent peer sessions, source-IP-based `USER peer@<IP>` authentication, and post-authentication command gating.
- Verified: valid and invalid peer identities, non-peer authentication, pre-auth rejection, concurrent peer sessions, and simultaneous FTP service.
- Remaining: repeat the Phase 5 build and peer integration tests on an actual Linux host.
- Deferred: peer discovery, synchronization, distributed `LIST`, and distributed `RETR`.

## Phase 6 — Peer discovery and synchronization

- Status: Implemented; peer LIST protocol verified on macOS, multi-host discovery pending school-network verification.
- Implemented: CIDR subnet parsing, bounded periodic scanning, authenticated peer inventory exchange, recursive file-list collection, and replacement/removal of peer snapshots.
- Verified: authenticated root and nested peer `LIST`, recursive inventory protocol, and discovery-worker startup with a temporary subnet configuration.
- Unavailable here: a distinct reachable second host for successful discovery and synchronization. The macOS loopback environment could not provide one without conflating it with the local server.
- Deferred: unified distributed `LIST` presentation and distributed `RETR`.

## Phase 7 — Distributed LIST

- Status: Implemented; merge logic and solo-mode regression verified on macOS, multi-host end-to-end verification pending.
- Implemented: local/peer listing merge, peer labeling, deterministic ordering, and local-name collision precedence.
- Verified: injected synchronized-snapshot merge tests and solo-mode FTP `LIST`.
- Unavailable here: end-to-end distributed `LIST` against a distinct reachable peer because multi-host discovery could not be exercised on this macOS host.
- Deferred: distributed `RETR` and later concurrency/cleanup work.

## Phase 8 — Distributed RETR

- Status: Implemented and relay-tested on macOS; direct zone-server redirection pending.
- Implemented: peer-side local `RETR`, owner lookup from synchronized snapshots, authenticated peer retrieval, and contact-server data relay.
- Verified: local and peer-side byte-for-byte transfers plus an in-process remote relay harness.
- Important protocol item: because the assignment orders `PASV` before `RETR`, the contact server cannot know the remote pathname during the initial PASV response. The current implementation preserves the client sequence and relays through Server A; direct peer PASV redirection must be validated on the SIUE zone servers.

## Phase 9 — Concurrency, disconnect detection, and cleanup

- Status: Complete on macOS; Linux and zone-server stress verification pending.
- Verified: 20 concurrent FTP sessions, abrupt FTP disconnects, abrupt P2P disconnects, continued service afterward, and transfer socket cleanup.
- Existing periodic scan failures remove stale peer snapshots.

## Phase 10 — Comprehensive testing and documentation

- Status: Complete for available macOS validation.
- Completed: clean warning-enabled build, success/failure integration tests, relay harness, concurrency stress tests, and documentation updates.
- Remaining for live validation: Linux VM build, multi-host discovery/synchronization, unified distributed LIST, direct PASV-redirection RETR, and peer-offline behavior on SIUE zone servers.
