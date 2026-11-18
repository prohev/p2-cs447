# Testing

## Milestone 1 checklist

- [ ] Linux `make` build with C++20, warnings, and pthreads.
- [ ] Valid configuration starts the listener.
- [ ] Greeting is `220`.
- [ ] `USER anonymous` returns `230`.
- [ ] Invalid `USER` returns `530` and closes.
- [ ] Commands are case-insensitive.
- [ ] `PWD`, `CWD`, and `CDUP` navigate within `db/`.
- [ ] Invalid directories return `550`.
- [ ] `QUIT` returns `221` and closes.
- [ ] Unauthenticated commands return `530`.
- [ ] Multiple clients remain independent.

## Execution record

No Linux/POSIX compilation or socket integration tests have been executed in the current Windows environment. These must be run in a Linux VM before Milestone 1 is complete.
