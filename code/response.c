#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
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

void HandleResponse(int fd, char *buf, int n) {
    char badresponse[1024] =
        "HTTP/1.1 404 Not Found\r\n\r\n<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\r\n<TITLE>Not Found</TITLE></HEAD><BODY>\r\n<H1>Not Found</H1>\r\n</BODY></HTML>\r\n\r\n";
    char goodresponse[1024] = "HTTP/1.1 200 OK\r\n\r\n";

    HttpRequest *req = parseHttpRequest(buf);
    //printf("%s\n", buf);
    //printHttpRequest(req);

    // Copy the path from the buffer
    if (buf != NULL && n >= 10)
    {
        //printf(" *** path %s, \n ", req->path );
        if (strstr(req->path, "python") == req->path) {
            printf("+"); fflush(stdout);
            if (!ExecutePythonScript(fd, req->path, req->query_params) )
                send(fd, badresponse, strlen(badresponse), 0);
            return;
        } else {
            int fh;
            fh = OpenFile(req);
            printf(".");
            if (fh > 0) {
                struct stat stat_buf; /* hold information about input file */
                send(fd, goodresponse, strlen(goodresponse), 0);
                /* size and permissions of fh */
                fstat(fh, &stat_buf);
                sendfile(fd, fh, NULL, stat_buf.st_size);
                close(fh);
            }
            else { 
                //printf(" not found \"%s\"", req->path );
                printf("-");
                send(fd, badresponse, strlen(badresponse), 0); }
        }
    }
    freeHttpRequest(req);
}
