

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
void printHttpRequest(HttpRequest *req);
void freeHttpRequest(HttpRequest *req);
