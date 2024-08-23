
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

HttpRequest* parseHttpRequest(const char *request);
void freeHttpRequest(HttpRequest *req);
void printHttpRequest(HttpRequest *req); 
