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
        char **args = malloc((param_count * 2 + 3) * sizeof(char *) * 2);
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
