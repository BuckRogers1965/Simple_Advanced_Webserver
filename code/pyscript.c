#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <Python.h>

#include "http_request.h"

// Root directory for Python scripts
char python_script_root[1024] = "."; 

void PythonInit(const char * script_root){
    // Set the root directory for Python scripts
    strncpy(python_script_root, script_root, sizeof(python_script_root) - 1);
    python_script_root[sizeof(python_script_root) - 1] = '\0';
}

void SetEnvironmentVariables(HttpRequest *request) {
    // Set request method
    setenv("REQUEST_METHOD", request->method, 1);

    // Set path info (you may need to adjust this depending on your setup)
    setenv("PATH_INFO", request->path, 1);

    // Set query string
    if (request->query_params) {
        // You might want to concatenate all key-value pairs into a single query string
        char query_string[1024] = "";
        KeyValue *current = request->query_params;
        while (current) {
            strcat(query_string, current->key);
            strcat(query_string, "=");
            strcat(query_string, current->value);
            if (current->next) strcat(query_string, "&");
            current = current->next;
        }
        setenv("QUERY_STRING", query_string, 1);
    } else {
        setenv("QUERY_STRING", "", 1);
    }

    // Set content type and length if available
    KeyValue *header = request->headers;
    while (header) {
        if (strcmp(header->key, "Content-Type") == 0) {
            setenv("CONTENT_TYPE", header->value, 1);
        } else if (strcmp(header->key, "Content-Length") == 0) {
            setenv("CONTENT_LENGTH", header->value, 1);
        }
        header = header->next;
    }

    // Set cookies if available
    if (request->cookies) {
        char cookie_string[1024] = "";
        KeyValue *current = request->cookies;
        while (current) {
            strcat(cookie_string, current->key);
            strcat(cookie_string, "=");
            strcat(cookie_string, current->value);
            if (current->next) strcat(cookie_string, "; ");
            current = current->next;
        }
        setenv("HTTP_COOKIE", cookie_string, 1);
    }

    // Set other relevant headers if needed
    // e.g., setenv("HTTP_USER_AGENT", <user-agent-value>, 1);
}


int ExecutePythonScript(int fd, HttpRequest *request)
{
    int pipefd[2]; // Pipe to capture the output of the Python script
    int stdin_pipe[2]; // Pipe for stdin
    pid_t pid;
    char buffer[1024];
    ssize_t bytes_read;

    // Create pipes
    if (pipe(pipefd) == -1 || pipe(stdin_pipe) == -1)
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
        close(stdin_pipe[1]); // Close unused write end of stdin pipe

        clearenv();
        SetEnvironmentVariables(request);

        // Redirect standard output to the write end of the pipe
        if (dup2(pipefd[1], STDOUT_FILENO) == -1)
        {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }
        close(pipefd[1]);

        // Redirect standard input to the read end of the stdin pipe
        if (dup2(stdin_pipe[0], STDIN_FILENO) == -1)
        {
            perror("dup2");
            _exit(EXIT_FAILURE);
        }
        close(stdin_pipe[0]);

        // Count the number of parameters
        int param_count = 0;
        KeyValue *current = request->query_params;
        while (current != NULL)
        {
            param_count++;
            current = current->next;
        }

        // Allocate memory for arguments array
        char **args = malloc((param_count * 2 + 3) * sizeof(char *));
        if (!args)
        {
            perror("malloc");
            _exit(EXIT_FAILURE);
        }

        // Set up the arguments array
        args[0] = "python3";
        args[1] = (char *)request->path;

        int arg_index = 2;
        current = request->query_params;
        while (current != NULL)
        {
            args[arg_index++] = current->key;
            args[arg_index++] = current->value;
            current = current->next;
        }
        args[arg_index] = NULL;

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
        close(stdin_pipe[0]); // Close unused read end of stdin pipe

        // Write body to child's stdin
        if (request->body) {
            write(stdin_pipe[1], request->body, strlen(request->body));
        }
        close(stdin_pipe[1]); // This sends EOF to the child's stdin

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