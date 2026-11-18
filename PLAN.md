# Distributed FTP Server Plan

Milestones are implemented sequentially. Milestone 1 establishes configuration parsing, concurrent client control sessions, anonymous authentication, and directory navigation. Passive data transfer and P2P behavior are deferred to later milestones.

The remote `RETR` direct-redirection ordering ambiguity remains documented: the assignment requires `PASV` before `RETR`, while the peer cannot be selected without the pathname.
