#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Define a structure for key-value pairs (used for headers, cookies, and form data)
typedef struct KeyValue {
    char *key;
    char *value;
    struct KeyValue *next;
} KeyValue;

// Define a structure for HTTP request
typedef struct HttpRequest {
    char *method;
    char *path;
    char *version;
    KeyValue *headers;
    KeyValue *cookies;
    KeyValue *query_params;
    KeyValue *form_data;
    char *body;
    long content_length;
} HttpRequest;

// Function to add a key-value pair to a list
void addKeyValue(KeyValue **list, const char *key, const char *value) {
    KeyValue *new_kv = (KeyValue*)malloc(sizeof(KeyValue));
    new_kv->key = strdup(key);
    new_kv->value = strdup(value);
    new_kv->next = *list;
    *list = new_kv;
}

// Function to parse the HTTP request
HttpRequest* parseHttpRequest(const char *request) {
    HttpRequest *req = (HttpRequest*)malloc(sizeof(HttpRequest));
    memset(req, 0, sizeof(HttpRequest));

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
    while ((line = strtok(NULL, "\r\n")) != NULL && strlen(line) > 0) {
        char *colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            char *value = colon + 1;
            while (*value == ' ') value++;
            addKeyValue(&req->headers, line, value);

            // Special handling for Content-Length and Cookie
            if (strcasecmp(line, "Content-Length") == 0) {
                req->content_length = atol(value);
            } else if (strcasecmp(line, "Cookie") == 0) {
                char *cookie_str = strdup(value);
                char *cookie_token = strtok(cookie_str, ";");
                while (cookie_token) {
                    char *eq = strchr(cookie_token, '=');
                    if (eq) {
                        *eq = '\0';
                        char *cookie_name = cookie_token;
                        char *cookie_value = eq + 1;
                        while (*cookie_name == ' ') cookie_name++;
                        addKeyValue(&req->cookies, cookie_name, cookie_value);
                    }
                    cookie_token = strtok(NULL, ";");
                }
                free(cookie_str);
            }
        }
    }

    // Parse query parameters
    char *query = strchr(req->path, '?');
    if (query) {
        *query = '\0';
        query++;
        char *query_copy = strdup(query);
        char *pair = strtok(query_copy, "&");
        while (pair) {
            char *eq = strchr(pair, '=');
            if (eq) {
                *eq = '\0';
                addKeyValue(&req->query_params, pair, eq + 1);
            }
            pair = strtok(NULL, "&");
        }
        free(query_copy);
    }

    // Trim leading '/' from path
    if (req->path && req->path[0] == '/') {
        char *trimmed_path = strdup(req->path + 1);
        free(req->path);
        req->path = trimmed_path;
    }

    // Handle POST request body
    if (strcasecmp(req->method, "POST") == 0 && req->content_length > 0) {
        char *body_start = strstr(request, "\r\n\r\n");
        if (body_start) {
            body_start += 4;  // Move past \r\n\r\n
            req->body = malloc(req->content_length + 1);
            memcpy(req->body, body_start, req->content_length);
            req->body[req->content_length] = '\0';

            // Parse form data if content-type is application/x-www-form-urlencoded
            KeyValue *content_type = req->headers;
            while (content_type && strcasecmp(content_type->key, "Content-Type") != 0)
                content_type = content_type->next;
            
            if (content_type && strstr(content_type->value, "application/x-www-form-urlencoded")) {
                char *form_data_copy = strdup(req->body);
                char *pair = strtok(form_data_copy, "&");
                while (pair) {
                    char *eq = strchr(pair, '=');
                    if (eq) {
                        *eq = '\0';
                        addKeyValue(&req->form_data, pair, eq + 1);
                    }
                    pair = strtok(NULL, "&");
                }
                free(form_data_copy);
            }
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
    free(req->body);

    KeyValue *current, *next;

    current = req->headers;
    while (current) {
        next = current->next;
        free(current->key);
        free(current->value);
        free(current);
        current = next;
    }

    current = req->cookies;
    while (current) {
        next = current->next;
        free(current->key);
        free(current->value);
        free(current);
        current = next;
    }

    current = req->query_params;
    while (current) {
        next = current->next;
        free(current->key);
        free(current->value);
        free(current);
        current = next;
    }

    current = req->form_data;
    while (current) {
        next = current->next;
        free(current->key);
        free(current->value);
        free(current);
        current = next;
    }

    free(req);
}

// Function to print the HTTP request (for debugging)
void printHttpRequest(HttpRequest *req) {
    printf("Method: %s\n", req->method);
    printf("Path: %s\n", req->path);
    printf("Version: %s\n", req->version);
    printf("Content-Length: %ld\n", req->content_length);

    printf("Headers:\n");
    for (KeyValue *kv = req->headers; kv != NULL; kv = kv->next) {
        printf("  %s: %s\n", kv->key, kv->value);
    }

    printf("Cookies:\n");
    for (KeyValue *kv = req->cookies; kv != NULL; kv = kv->next) {
        printf("  %s: %s\n", kv->key, kv->value);
    }

    printf("Query Parameters:\n");
    for (KeyValue *kv = req->query_params; kv != NULL; kv = kv->next) {
        printf("  %s: %s\n", kv->key, kv->value);
    }

    printf("Form Data:\n");
    for (KeyValue *kv = req->form_data; kv != NULL; kv = kv->next) {
        printf("  %s: %s\n", kv->key, kv->value);
    }

    if (req->body) {
        printf("Body:\n%s\n", req->body);
    }
}