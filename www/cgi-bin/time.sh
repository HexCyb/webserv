#!/bin/sh
# Shell CGI: headers, an empty line, then the body.
printf 'Content-Type: text/plain; charset=utf-8\r\n\r\n'
echo "server time: $(date)"
echo "method: $REQUEST_METHOD"
