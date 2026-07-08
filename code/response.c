#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "conn.h"
#include "http_request.h"
#include "pyscript.h"

int OpenFile(HttpRequest *req) {
    char *index = "index.html";
    char path[1024];

    if (req == NULL || req->path == NULL)
        return 0;

    // Copy the path from the request
    strncpy(path, req->path, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\x00'; // Ensure null termination

    // If the last character is a slash, append index.html
    int len = strlen(path);
    if (path[len - 1] == '/') {
        strncat(path, index, sizeof(path) - len - 1);
    }

    //printf("Found path: \"%s\"\n", path);

    // Open the file
    return open(path, O_RDONLY); // the +1 removes the leading slash
}

void HandleResponse(Conn *conn, char *buf, int n) {
    char badresponse[1024] =
        "HTTP/1.1 404 Not Found\r\n\r\n<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\r\n<TITLE>Not Found</TITLE></HEAD><BODY>\r\n<H1>Not Found</H1>\r\n</BODY></HTML>\r\n\r\n";
    char goodresponse[1024] = "HTTP/1.1 200 OK\r\n\r\n";

    HttpRequest *request = parseHttpRequest(buf);
    //printf("%s\n", buf);
    //printHttpRequest(request);

    // Copy the path from the buffer
    if (buf != NULL && n >= 10)
    {
        //printf(" *** path %s, \n ", req->path );
        if (strstr(request->path, "python") == request->path) {
            printf("+"); fflush(stdout);
            if (!ExecutePythonScript(conn, request) )
                ConnWrite(conn, badresponse, strlen(badresponse));
            freeHttpRequest(request);
            return;
        } else {
            int fh;
            fh = OpenFile(request);
            printf(".");
            if (fh > 0) {
                struct stat stat_buf; /* hold information about input file */
                ConnWrite(conn, goodresponse, strlen(goodresponse));
                /* size and permissions of fh */
                fstat(fh, &stat_buf);
                ConnSendFile(conn, fh, stat_buf.st_size);
                close(fh);
            }
            else {
                //printf(" not found \"%s\"", req->path );
                printf("-");
                ConnWrite(conn, badresponse, strlen(badresponse)); }
        }
    }
    freeHttpRequest(request);
}
