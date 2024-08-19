#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sched.h>
#include <pthread.h>
#include <Python.h>
#include <sys/wait.h>

typedef struct thread
{
    int Running;
    int fd;
    pthread_t thread;
    char buffer[1024];
    struct thread *next;
    struct thread *prev;
} thread;

struct thread *freelist;
struct thread *worklist;
pthread_mutex_t freelist_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t worklist_mutex = PTHREAD_MUTEX_INITIALIZER;

char python_script_root[1024] = "."; // Root directory for Python scripts

// Define a structure for parameters
typedef struct Parameter {
    char *key;
    char *value;
    struct Parameter *next;
} Parameter;

// Define a structure for HTTP request
typedef struct HttpRequest {
    char *method;
    char *path;
    char *version;
    char *host;
    char *connection;
    char *user_agent;
    char *accept;
    char *sec_gpc;
    char *referer;
    char *accept_encoding;
    char *accept_language;
    Parameter *params;
} HttpRequest;

// Function prototypes
HttpRequest* parseHttpRequest(const char *request);
void freeHttpRequest(HttpRequest *req);

// Function to parse the HTTP request
HttpRequest* parseHttpRequest(const char *request) {
    HttpRequest *req = (HttpRequest*)malloc(sizeof(HttpRequest));
    memset(req, 0, sizeof(HttpRequest)); // Initialize all fields to NULL/0
    req->params = NULL;

    // Allocate a copy of the request to modify
    char *request_copy = strdup(request);
    char *line = strtok(request_copy, "\r\n");

    // Parse the first line (method, path, version)
    if (line) {
        req->method = strndup(line, strchr(line, ' ') - line);
        line += strlen(req->method) + 1;
        req->path = strndup(line, strchr(line, ' ') - line);
        line += strlen(req->path) + 1;
        req->version = strdup(line);
    }

    // Parse the headers
    while ((line = strtok(NULL, "\r\n")) != NULL) {
        if (strncmp(line, "Host:", 5) == 0) {
            req->host = strdup(line + 6);
        } else if (strncmp(line, "Connection:", 11) == 0) {
            req->connection = strdup(line + 12);
        } else if (strncmp(line, "User-Agent:", 11) == 0) {
            req->user_agent = strdup(line + 12);
        } else if (strncmp(line, "Accept:", 7) == 0) {
            req->accept = strdup(line + 8);
        } else if (strncmp(line, "Sec-GPC:", 8) == 0) {
            req->sec_gpc = strdup(line + 9);
        } else if (strncmp(line, "Referer:", 8) == 0) {
            req->referer = strdup(line + 9);
        } else if (strncmp(line, "Accept-Encoding:", 16) == 0) {
            req->accept_encoding = strdup(line + 17);
        } else if (strncmp(line, "Accept-Language:", 16) == 0) {
            req->accept_language = strdup(line + 17);
        }
    }

    // Trim leading '/' from path
    if (req->path && req->path[0] == '/') {
        char *trimmed_path = strdup(req->path + 1);
        free(req->path);
        req->path = trimmed_path;
    }

    // Parse the query parameters in the path
    char *query = strchr(req->path, '?');
    if (query) {
        *query = '\0'; // Terminate the path string
        query++;
        while (query && *query) {
            char *amp = strchr(query, '&');
            if (amp) *amp = '\0'; // Temporarily end the current parameter
            char *eq = strchr(query, '=');
            if (eq) {
                *eq = '\0'; // Temporarily end the key string
                Parameter *param = (Parameter*)malloc(sizeof(Parameter));
                param->key = strdup(query);
                param->value = strdup(eq + 1);
                param->next = req->params;
                req->params = param;
            }
            query = amp ? amp + 1 : NULL;
        }
    }

    free(request_copy);
    return req;
}

// Function to free the HTTP request
void freeHttpRequest(HttpRequest *req) {
    if (!req) return;

    free(req->method);
    free(req->path);
    free(req->version);
    free(req->host);
    free(req->connection);
    free(req->user_agent);
    free(req->accept);
    free(req->sec_gpc);
    free(req->referer);
    free(req->accept_encoding);
    free(req->accept_language);

    // Free the parameters
    Parameter *param = req->params;
    while (param) {
        Parameter *next = param->next;
        free(param->key);
        free(param->value);
        free(param);
        param = next;
    }

    free(req);
}

void printHttpRequest(HttpRequest *req) {
    // Print parsed result (for demonstration)
    if (req->method) printf("Method: %s\n", req->method);
    if (req->path) printf("Path: %s\n", req->path);
    if (req->version) printf("Version: %s\n", req->version);
    if (req->host) printf("Host: %s\n", req->host);
    if (req->connection) printf("Connection: %s\n", req->connection);
    if (req->user_agent) printf("User-Agent: %s\n", req->user_agent);
    if (req->accept) printf("Accept: %s\n", req->accept);
    if (req->sec_gpc) printf("Sec-GPC: %s\n", req->sec_gpc);
    if (req->referer) printf("Referer: %s\n", req->referer);
    if (req->accept_encoding) printf("Accept-Encoding: %s\n", req->accept_encoding);
    if (req->accept_language) printf("Accept-Language: %s\n", req->accept_language);

    // Print parameters
    Parameter *param = req->params;
    while (param) {
        if (param->key && param->value) 
            printf("Parameter: %s = %s\n", param->key, param->value);
        param = param->next;
    }
}

int ExecutePythonScript(int fd, const char *script_path, Parameter *params)
{
    int pipefd[2]; // Pipe to capture the output of the Python script
    pid_t pid;
    char buffer[1024];
    ssize_t bytes_read;

    // Create a pipe
    if (pipe(pipefd) == -1)
    {
        perror("pipe");
        return 0;
    }

    // Fork a new process
    pid = fork();
    if (pid == -1)
    {
        perror("fork");
        return 0;
    }

    if (pid == 0)
    {
        // Child process
        close(pipefd[0]); // Close unused read end

        // Redirect standard output to the write end of the pipe
        if (dup2(pipefd[1], STDOUT_FILENO) == -1)
        {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }
        close(pipefd[1]);

        // Count the number of parameters
        int param_count = 0;
        Parameter *current = params;
        while (current != NULL)
        {
            param_count++;
            current = current->next;
        }

        // Allocate memory for arguments array
        char **args = malloc((param_count * 2 + 3) * sizeof(char *)*2);
        if (!args)
        {
            perror("malloc");
            _exit(EXIT_FAILURE);
        }

        // Set up the arguments array
        args[0] = "python3";
        args[1] = (char *)script_path;

        int arg_index = 2;
        current = params;
        while (current != NULL)
        {
            args[arg_index++] = current->key;
            args[arg_index++] = current->value;
            current = current->next;
        }
        args[arg_index] = NULL;

        // Add debug output
        for (int i = 0; args[i] != NULL; i++) 
            fprintf(stderr, "args[%d]: %s\n", i, args[i]);
            
        // Execute the Python script
        execvp("python3", args);

        // If execvp returns, it must have failed
        perror("execvp");
        free(args);
        _exit(EXIT_FAILURE);
    }
    else
    {
        // Parent process
        close(pipefd[1]); // Close unused write end

        // Read the output of the Python script from the pipe
        while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0)
        {
            buffer[bytes_read] = '\0'; // Null-terminate the buffer
            if (send(fd, buffer, bytes_read, 0) == -1)
            {
                perror("send");
                close(pipefd[0]);
                waitpid(pid, NULL, 0);
                return 0;
            }
        }
        close(pipefd[0]);

        // Wait for the child process to exit
        int status;
        if (waitpid(pid, &status, 0) == -1)
        {
            perror("waitpid");
            return 0;
        }

        // Check if the child process terminated successfully
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        {
            return 1; // Success
        }
        else
        {
            return 0; // Failure
        }
    }
}

int ExecutePythonScriptold2(int fd, const char *script_path)
{
    int pipefd[2]; // Pipe to capture the output of the Python script
    pid_t pid;
    char buffer[1024];
    ssize_t bytes_read;

    // Create a pipe
    if (pipe(pipefd) == -1)
    {
        perror("pipe");
        return 0;
    }

    // Fork a new process
    pid = fork();
    if (pid == -1)
    {
        perror("fork");
        return 0;
    }

    if (pid == 0)
    {
        // Child process
        close(pipefd[0]); // Close unused read end

        // Redirect standard output to the write end of the pipe
        if (dup2(pipefd[1], STDOUT_FILENO) == -1)
        {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }
        close(pipefd[1]);

        // Execute the Python script
        execlp("python3", "python3", script_path, (char *)NULL);

        // If execlp returns, it must have failed
        perror("execlp");
        _exit(EXIT_FAILURE);
    }
    else
    {
        // Parent process
        close(pipefd[1]); // Close unused write end

        // Read the output of the Python script from the pipe
        while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0)
        {
            buffer[bytes_read] = '\0'; // Null-terminate the buffer
            if (send(fd, buffer, bytes_read, 0) == -1)
            {
                perror("send");
                close(pipefd[0]);
                waitpid(pid, NULL, 0);
                return 0;
            }
        }
        close(pipefd[0]);

        // Wait for the child process to exit
        int status;
        if (waitpid(pid, &status, 0) == -1)
        {
            perror("waitpid");
            return 0;
        }

        // Check if the child process terminated successfully
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        {
            return 1; // Success
        }
        else
        {
            return 0; // Failure
        }
    }
}

void ExecutePythonScriptold(int fd, const char *script_path)
{
    int pipefd[2]; // Pipe to capture the output of the Python script
    pid_t pid;
    char buffer[1024];
    ssize_t bytes_read;

    // Create a pipe
    if (pipe(pipefd) == -1)
    {
        perror("pipe");
        return;
    }

    // Fork a new process
    pid = fork();
    if (pid == -1)
    {
        perror("fork");
        return;
    }

    if (pid == 0)
    {
        // Child process
        close(pipefd[0]); // Close unused read end

        // Redirect standard output to the write end of the pipe
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        // Execute the Python script
        execlp("python3", "python3", script_path, (char *)NULL);

        // If execlp returns, it must have failed
        perror("execlp");
        _exit(EXIT_FAILURE);
    }
    else
    {
        // Parent process
        close(pipefd[1]); // Close unused write end

        // Read the output of the Python script from the pipe
        while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0)
        {
            buffer[bytes_read] = '\0'; // Null-terminate the buffer
            send(fd, buffer, bytes_read, 0);
        }
        close(pipefd[0]);

        // Wait for the child process to exit
        waitpid(pid, NULL, 0);
    }
}

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

    printf("Found path: \"%s\"\n", path);

    // Open the file
    return open(path, O_RDONLY); // the +1 removes the leading slash
}

void HandleResponse(int fd, char *buf, int n)
{
    char badresponse[1024] =
        "HTTP/1.1 404 Not Found\r\n\r\n<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\r\n<TITLE>Not Found</TITLE></HEAD><BODY>\r\n<H1>Not Found</H1>\r\n</BODY></HTML>\r\n\r\n";
    char goodresponse[1024] = "HTTP/1.1 200 OK\r\n\r\n";
    char path[1024];
    int is_python_script = 0;

    HttpRequest *req = parseHttpRequest(buf);
    printHttpRequest(req);

    // Copy the path from the buffer
    if (buf != NULL && n >= 10)
    {
        //printf(" *** path %s, \n ", req->path );
        if (strstr(req->path, "python") == req->path)
        {
            // Execute the Python script
            if (!ExecutePythonScript(fd, req->path, req->params) )
                send(fd, badresponse, strlen(badresponse), 0);
            return;
        }
        else
        {
            int fh;
            fh = OpenFile(req);

            if (fh > 0)
            {
                struct stat stat_buf; /* hold information about input file */
                send(fd, goodresponse, strlen(goodresponse), 0);
                /* size and permissions of fh */
                fstat(fh, &stat_buf);
                sendfile(fd, fh, NULL, stat_buf.st_size);
                close(fh);
            }
            else
            {
                send(fd, badresponse, strlen(badresponse), 0);
            }
        }
    }
    freeHttpRequest(req);
}
/*
void HandleResonseold(int fd, char *buf, int n)
{
    char badresponse[1024] = "HTTP/1.1 404 Not Found\r\n\r\n<HTML><HEAD><meta http-equiv=\"content-type\" content=\"text/html;charset=utf-8\">\r\n<TITLE>Not Found</TITLE></HEAD><BODY>\r\n<H1>Not Found</H1>\r\n</BODY></HTML>\r\n\r\n";
    char goodresponse[1024] = "HTTP/1.1 200 OK\r\n\r\n";
    int fh;

    // Check if the request matches the Python script path
    if (strncmp(buf + 4, "/run_python", 11) == 0)
    {
        char script_path[1024];
        snprintf(script_path, sizeof(script_path), "%s/%s", python_script_root, buf + 15); // Adjusted to handle the URL path

        // Replace any '?' with '\0' to truncate query parameters
        char *query = strchr(script_path, '?');
        if (query)
        {
            *query = '\0';
        }

        ExecutePythonScript(fd, script_path);
        return;
    }

    fh = OpenFile(buf, n);

    if (fh > 0)
    {
        struct stat stat_buf; // hold information about input file 
        send(fd, goodresponse, strlen(goodresponse), 0);
        // size and permissions of fh 
        fstat(fh, &stat_buf);
        sendfile(fd, fh, NULL, stat_buf.st_size);
        close(fh);
    }
    else
    {
        send(fd, badresponse, strlen(badresponse), 0);
    }
} */

void *handleRequest(void *arg)
{
    struct thread *t = (thread *)arg;
    int n;

    for (;;)
    {
        while (!t->Running)
        {
            usleep(2000);
        }

        n = recv(t->fd, t->buffer, 1023, 0);

        if (n > 0)
        { // got a request, process it.
            HandleResponse(t->fd, t->buffer, n);
        }

        close(t->fd);
        t->Running = 0;

        pthread_mutex_lock(&worklist_mutex);
        if (worklist == t)
            worklist = t->next;
        if (t->next)
            t->next->prev = t->prev;
        if (t->prev)
            t->prev->next = t->next;
        pthread_mutex_unlock(&worklist_mutex);

        pthread_mutex_lock(&freelist_mutex);
        t->next = freelist;
        if (freelist)
            freelist->prev = t;
        freelist = t;
        pthread_mutex_unlock(&freelist_mutex);
    }
}

void ThreadInitialize(int ThreadCount, const char *script_root)
{
    struct thread *t;
    int i;

    freelist = worklist = NULL;

    // Set the root directory for Python scripts
    strncpy(python_script_root, script_root, sizeof(python_script_root) - 1);
    python_script_root[sizeof(python_script_root) - 1] = '\0';

    for (i = 0; i < ThreadCount; i++)
    {
        t = (struct thread *)calloc(1, sizeof(thread));

        pthread_mutex_lock(&freelist_mutex);
        // add new thread structure to freelist
        t->next = freelist;
        freelist = t;
        pthread_mutex_unlock(&freelist_mutex);

        if (pthread_create(&t->thread, NULL, &handleRequest, t) != 0)
        {
            perror("pthread create failed\n");
            exit(1);
        }
    }
}

void ThreadCheck()
{
    pthread_mutex_lock(&worklist_mutex);
    struct thread *t = worklist;
    while (t != NULL)
    {
        if (!t->Running)
        {
            // remove thread structure from worklist
            if (worklist == t)
                worklist = t->next;
            if (t->next)
                t->next->prev = t->prev;
            if (t->prev)
                t->prev->next = t->next;

            pthread_mutex_unlock(&worklist_mutex);
            pthread_mutex_lock(&freelist_mutex);
            // add thread structure to freelist
            t->next = freelist;
            if (freelist)
                freelist->prev = t;
            freelist = t;
            pthread_mutex_unlock(&freelist_mutex);
            return;
        }
        t = t->next;
    }
    pthread_mutex_unlock(&worklist_mutex);
}

void ThreadHandleResponse(int fd)
{
    struct thread *t;

    /* wait for any child process to free up */
    while (1)
    {
        pthread_mutex_lock(&freelist_mutex);
        if (freelist != NULL)
        {
            // remove thread structure from freelist
            t = freelist;
            freelist = t->next;
            if (freelist)
                freelist->prev = NULL;
            pthread_mutex_unlock(&freelist_mutex);

            pthread_mutex_lock(&worklist_mutex);
            // add thread structure to worklist
            t->next = worklist;
            if (worklist)
                worklist->prev = t;
            worklist = t;
            pthread_mutex_unlock(&worklist_mutex);

            // setup thread and run it
            t->fd = fd;
            t->Running = 1;
            return;
        }
        pthread_mutex_unlock(&freelist_mutex);

        ThreadCheck();
        sched_yield();
    }
}
