# Unsafe API Rules

This is a practical baseline list for spotlighting low-hanging safety improvements.

Format:
- `unsafe` and `safe alternative` are used by the checker.
- `comment` gives context and caveats.

## 1) Buffer Handling And Formatting (overflow-prone)

| unsafe | safe alternative | comment |
|---|---|---|
| gets | fgets | never use gets |
| strcpy | snprintf | bounded copy with explicit destination size |
| strncpy | snprintf | often leaves destination unterminated; prefer explicit size-aware copy |
| strcat | snprintf | bounded append with explicit destination size |
| strncat | snprintf | still error-prone for remaining-buffer math |
| sprintf | snprintf | bounded formatting |
| vsprintf | vsnprintf | bounded variadic formatting |
| scanf | fgets + strto*/sscanf with width limits | avoid unbounded `%s` and implicit parsing failures |
| fscanf | fgets + strto*/sscanf with width limits | avoid unbounded `%s` and implicit parsing failures |
| sscanf | strto*/manual parse | easier error handling and range validation |
| vscanf | fgets + parse | avoid unbounded formatted input |
| vfscanf | fgets + parse | avoid unbounded formatted input |
| vsscanf | strto*/manual parse | explicit parse errors are easier to handle |

## 2) Numeric Conversion Without Error Handling

| unsafe | safe alternative | comment |
|---|---|---|
| atoi | strtol | adds error handling and range checks |
| atol | strtol | adds error handling and range checks |
| atoll | strtoll | adds error handling and range checks |
| atof | strtod | adds error handling and locale control |

## 3) Shared-State Tokenization / RNG

| unsafe | safe alternative | comment |
|---|---|---|
| strtok | strtok_r | thread-safe and re-entrant tokenization |
| rand | random_r or arc4random | shared global RNG state, weak statistical quality |

## 4) Temporary Files And Shell Execution

| unsafe | safe alternative | comment |
|---|---|---|
| tmpnam | mkstemp | race-prone filename generation |
| tempnam | mkstemp | race-prone filename generation |
| mktemp | mkstemp | race-prone filename generation |
| system | fork+execve or posix_spawn | shell injection and environment side effects |
| popen | pipe + fork + execve | shell injection risk and weaker control |

## 5) Thread-Unsafe Time / Error APIs

| unsafe | safe alternative | comment |
|---|---|---|
| asctime | strftime | avoid static internal buffer |
| ctime | strftime | avoid static internal buffer |
| gmtime | gmtime_r | thread-safe time conversion |
| localtime | localtime_r | thread-safe time conversion |
| strerror | strerror_r | thread-safe error-message conversion |

## 6) Legacy DNS / Network Lookup APIs (thread-unsafe and obsolete)

| unsafe | safe alternative | comment |
|---|---|---|
| gethostbyname | getaddrinfo | obsolete and not thread-safe |
| gethostbyaddr | getnameinfo/getaddrinfo | obsolete and not thread-safe |
| getservbyname | getaddrinfo or getservbyname_r | obsolete shared-state API |
| getservbyport | getaddrinfo or getservbyport_r | obsolete shared-state API |
| getprotobyname | getprotobyname_r | shared-state API |
| getprotobynumber | getprotobynumber_r | shared-state API |

## 7) Signal-Handling API Preference

| unsafe | safe alternative | comment |
|---|---|---|
| signal | sigaction | clearer semantics and stronger handler control |
