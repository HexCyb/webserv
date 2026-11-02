#!/usr/bin/env bash
# Starts ./webserv with conf/default.conf and checks real answers with curl.
cd "$(dirname "$0")/.." || exit 1
make -s || exit 1

./webserv conf/default.conf > /tmp/webserv-test.log 2>&1 &
PID=$!
trap 'kill $PID 2>/dev/null' EXIT
sleep 0.5

PASS=0
FAIL=0
check() {   # check <name> <expected> <actual>
    if [ "$2" = "$3" ]; then
        PASS=$((PASS + 1)); printf '  \033[32mPASS\033[0m  %s\n' "$1"
    else
        FAIL=$((FAIL + 1)); printf '  \033[31mFAIL\033[0m  %s (expected "%s", got "%s")\n' "$1" "$2" "$3"
    fi
}
code() { curl -s -o /dev/null -w '%{http_code}' "$@"; }

URL=http://127.0.0.1:8080

echo "static files"
check "GET /"                        200 "$(code $URL/)"
check "HEAD /"                       200 "$(code -I $URL/)"
check "custom 404 page"              404 "$(code $URL/nope)"
check "404 page content"             "<h1>404</h1>" "$(curl -s $URL/nope | grep -o '<h1>404</h1>')"
check "autoindex /files/"            200 "$(code $URL/files/)"
check "folder without slash"         301 "$(code $URL/files)"
check "MIME type of .json"           "application/json" "$(curl -s -o /dev/null -w '%{content_type}' $URL/files/sample.json)"
check "redirect /old"                301 "$(code $URL/old)"
check "keep-alive: 2 requests, 1 connection" "10" "$(curl -s -o /dev/null -o /dev/null -w '%{num_connects}' $URL/ $URL/files/hello.txt)"

echo "methods and errors"
check "POST not allowed on /"        405 "$(code -X POST -d x $URL/)"
check "unknown method"               501 "$(code -X PATCH $URL/)"
check "path traversal"               400 "$(code --path-as-is $URL/../../etc/passwd)"
check "body over the limit"          413 "$(head -c 2000000 /dev/zero | code -H 'Expect:' --data-binary @- $URL/cgi-bin/echo.py)"

echo "uploads and DELETE"
check "upload (multipart form)"      201 "$(code -F 'file=@www/files/hello.txt;filename=test-upload.txt' $URL/uploads)"
check "uploaded file is served"      "$(cat www/files/hello.txt)" "$(curl -s $URL/uploads/test-upload.txt)"
check "upload (raw body)"            201 "$(code --data-binary 'raw data' $URL/uploads/raw.txt)"
check "DELETE a file"                204 "$(code -X DELETE $URL/uploads/test-upload.txt)"
check "DELETE it again"              404 "$(code -X DELETE $URL/uploads/test-upload.txt)"
rm -f www/uploads/raw.txt

echo "CGI"
check "GET with a query string"      200 "$(code "$URL/cgi-bin/hello.py?name=42")"
check "POST body reaches the script" "received: 5 bytes" "$(curl -s -d hello $URL/cgi-bin/echo.py | sed -n 2p)"
check "chunked body is unchunked"    "received: 11 bytes" "$(printf 'hello world' | curl -s -H 'Transfer-Encoding: chunked' --data-binary @- $URL/cgi-bin/echo.py | sed -n 2p)"
check "shell CGI"                    200 "$(code $URL/cgi-bin/time.sh)"
check "endless script -> timeout"    504 "$(code --max-time 15 $URL/cgi-bin/loop.py)"

echo "several servers"
check "virtual host blog.local"      "blog.local" "$(curl -s -H 'Host: blog.local' $URL/ | grep -o '<h1>blog.local</h1>' | sed 's/<[^>]*>//g')"
check "second port 8081"             200 "$(code http://127.0.0.1:8081/)"

echo
echo "  $PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
