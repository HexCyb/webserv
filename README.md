# webserv

An HTTP/1.1 web server written from scratch in **C++98**. A single `poll()` loop drives non-blocking sockets and CGI pipes, and an nginx-style config file sets up servers, ports, virtual hosts and per-location rules.

> Originally completed at **42 Türkiye** as part of the common core. This repository is a 2026 rewrite, because the original code was lost.
> 42 Türkiye'de tamamladığım projenin 2026'da yeniden yazılmış hali; orijinal kod kayboldu.

## Features

| | |
|---|---|
| **Event loop** | One `poll()` watches every listening socket, client and CGI pipe, so nothing ever blocks |
| **HTTP** | GET, HEAD, POST, DELETE · keep-alive · pipelining · chunked request bodies · HTTP/1.0 clients |
| **Static files** | MIME types, index files, generated directory listings (`autoindex`) |
| **Uploads** | `multipart/form-data` from browser forms, or a raw body (`curl --data-binary`) |
| **CGI** | One interpreter per extension (Python, sh, php-cgi…), `PATH_INFO`, body on stdin, 5 s timeout → 504 |
| **Config** | Several servers and ports, virtual hosts by `server_name`, rules per location, custom error pages |
| **Safety** | `/../` escapes → 400 · body limit → 413 · header limit → 431 · idle clients → 408 · request smuggling headers → 400 |

Tested with 25 curl checks and a load test (10,000 requests from 200 parallel clients, all `200 OK`). It also runs clean under AddressSanitizer and UBSan.

## Build and run

```sh
make
./webserv conf/default.conf        # then open http://localhost:8080
```

`make test` runs the test suite. Run `python3 tests/stress.py 200 50` against a running server for the load test.

## Configuration

```nginx
server {
    listen      127.0.0.1:8080;            # [host:]port, may appear several times
    server_name localhost;                 # names for virtual hosting
    root        ./www;
    index       index.html;
    client_max_body_size 1M;
    error_page  404 /errors/404.html;

    location /uploads {
        methods      GET POST DELETE;
        autoindex    on;
        upload_store ./www/uploads;
        client_max_body_size 50M;
    }
    location /cgi-bin {
        methods GET POST;
        cgi .py /usr/bin/python3;
        cgi .sh /bin/sh;
    }
    location /old {
        return 301 /;
    }
}
```

| Directive | Where | Meaning |
|---|---|---|
| `listen [host:]port` | server | Address to accept connections on |
| `server_name name…` | server | Host names this block answers for. The first block on an address is the default |
| `root dir` | both | Folder that URLs map to (`root + path`, like nginx) |
| `index file…` | both | Files tried when a folder is requested |
| `autoindex on\|off` | both | Directory listing when no index file exists |
| `methods GET POST…` | both | Allowed methods (HEAD follows GET). Others get 405 |
| `client_max_body_size n[K\|M\|G]` | both | Largest accepted body. 0 means no limit |
| `error_page code… file` | server | Custom page for those status codes |
| `return code url` | location | Redirect (301, 302, 303, 307, 308) |
| `upload_store dir` | location | Folder where POSTed files are saved |
| `cgi .ext interpreter` | location | Run files with this extension through the interpreter |

A location's own values override the server's, and anything it leaves out is inherited. The longest matching location wins.

## How it works

```
                 ┌──────────────────────── poll() ─────────────────────────┐
 listen sockets  │ accept() → new Client                                   │
 client sockets  │ recv() → Request parser (incremental) → Handler → send()│
 CGI pipes       │ body → script stdin        script stdout → Response     │
                 └─────────────────────────────────────────────────────────┘
```

1. **Config** turns the file into `ServerConfig` and `LocationConfig` objects and reports mistakes with their line number.
2. **Server** opens one socket per `host:port` and builds the `pollfd` list on each turn. A file descriptor is only read or written after `poll()` says it is ready.
3. **Request** is fed bytes as they arrive. It parses the request line and headers, then reads the body by `Content-Length` or by chunks. Bytes that belong to the next request stay in the buffer (pipelining).
4. **Handler** picks the location (longest prefix) and then checks redirects and methods. It then serves a file, lists a folder, saves an upload, deletes a file or hands the request to CGI.
5. **Cgi** forks, connects two non-blocking pipes and runs the script in its own folder. The pipes are added to the same `poll()` loop. The script's headers and body become the HTTP response.

## Project structure

```
include/   Config  Request  Response  Handler  Cgi  Server  utils   (.hpp)
src/       the matching .cpp files + main.cpp
conf/      default.conf: 3 servers, 2 ports, a virtual host
www/       demo site: index, error pages, files/, uploads/, cgi-bin/
tests/     run_tests.sh (curl) · stress.py (load test)
```
