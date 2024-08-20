#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
