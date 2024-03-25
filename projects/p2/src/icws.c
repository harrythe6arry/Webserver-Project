#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h> // Include for getopt_long
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <ctype.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include "pcsa_net.h" // Assume this is correctly implemented
#include <pthread.h>
#include "parse.h"
#include <time.h> // For formatting dates
#include <poll.h>
#include <stdbool.h>


typedef struct Task {
    int connFd;
    char *rootFolder;
    Request *request;
} Task;


void serve_file(int connFd, char *filepath, char *mimeType, int headOnly);
void send_404(int connFd);
void send_501(int connFd);
char *determine_mime_type(const char *uri);
void execute(Task *task);
void send_505(int connFd);
void send_400(int connFd);
void *startThread(void *args);
void submit(Task task);
void send_408(int connFd);
void send_500(int connFd);
void options_usage(int argc, char **argv);

char *port = ""; 
char *rootFolder = "";
int numThreads = 0;
int timeout = 0;
char *cgiProgram = "";

#define BUF_SIZE 8192
#define MAX 256

Task taskqueue[MAX];
int taskcount = 0;

#define CHECK(msg) printf("CHECK: %s\n", msg); 

pthread_mutex_t mutexqueue = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condqueue = PTHREAD_COND_INITIALIZER;

void submit(Task task) {
    // CHECK("task submitting to the queue");
    pthread_mutex_lock(&mutexqueue);
    taskqueue[taskcount++] = task;
    task = taskqueue[0];
    pthread_mutex_unlock(&mutexqueue);
    pthread_cond_signal(&condqueue);
    // CHECK("Task submitted to the queue");
}


void *startThread(void *args) {
    while (1) {
        // CHECK("Thread is waiting for a task"); 
        Task task;
        pthread_mutex_lock(&mutexqueue);
        while (taskcount == 0)
        {
            pthread_cond_wait(&condqueue, &mutexqueue);
        }
        task = taskqueue[0];
        int i;
        for (i = 0; i < taskcount - 1; i++)
        {
            taskqueue[i] = taskqueue[i + 1];
        }
        taskcount--;
        pthread_mutex_unlock(&mutexqueue);
        // CHECK("Thread is executing a task"); 
        execute(&task); // execute the task
        // CHECK("Thread is done executing a task"); 
    }
}



void execute(Task *task) {
    int connFd = task->connFd;
    char *rootFolder = task->rootFolder;
    Request *request = task->request;

    if (!request) {
        send_501(connFd); // Send 501 on parse failure
        close(connFd);
        return;
    }

    char cgi_checker[BUF_SIZE];
    strcpy(cgi_checker, request->http_uri);

    char *ext = strrchr(request->http_uri, '.');
    ext = ext ? ext + 1 : ""; // Extract file extension
    char *mimeType = determine_mime_type(ext);
    mimeType = mimeType ? mimeType : ""; // Default to binary stream if unknown

    if (strcmp(request->http_version, "HTTP/1.1")) {
        send_505(connFd); // HTTP Version Not Supported
    }

    else if (strncmp(request->http_uri, "/cgi/", 5) == 0) {
        // Handle CGI script execution
        CHECK("CHECKing through CGI script"); 
        handle_cgi(task);
        
    }  
    else if (strcasecmp(request->http_method, "GET") != 0 && strcasecmp(request->http_method, "HEAD") != 0) {
        send_501(connFd); // Not Implemented
    }

    
    else if (strcasecmp(request->http_method, "GET") == 0 || strcasecmp(request->http_method, "HEAD") == 0) {
            char filepath[BUF_SIZE];
            snprintf(filepath, sizeof(filepath), "%s%s", rootFolder, request->http_uri);
            serve_file(connFd, filepath, mimeType, strcasecmp(request->http_method, "HEAD") == 0);

    } else {
        send_400(connFd); // Bad Request
    }
    if (request->headers) {
        free(request->headers); // Free allocated headers
    }

    free(request); // Free the request structure
    printf("close connection after checking request\n"); 
    close(connFd);
}


void fail_exit(char *msg) { fprintf(stderr, "%s\n", msg); exit(-1); }


void set_up_cgi(Task *task) {

    CHECK("Setting up CGI environment variables in the function");

    Request *request = task->request;
    int connFd = task->connFd;
    printf("connFd is %d\n", connFd);

    char* header_name; 
    char* header_value; 

    for (int i = 0; i < request->header_count; i++) {
        header_name = request->headers[i].header_name;
        header_value = request->headers[i].header_value;
        
        if (!strcasecmp(header_name, "CONNECTION")) {
            setenv("HTTP_CONNECTION", header_value, 1);
        } else if (!strcasecmp(header_name, "ACCEPT")) {
            setenv("HTTP_ACCEPT", header_value, 1);
        } else if (!strcasecmp(header_name, "REFERER")) {
            setenv("HTTP_REFERER", header_value, 1);
        } else if (!strcasecmp(header_name, "ACCEPT-ENCODING")) {
            setenv("HTTP_ACCEPT_ENCODING", header_value, 1);
        } else if (!strcasecmp(header_name, "ACCEPT-LANGUAGE")) {
            setenv("HTTP_ACCEPT_LANGUAGE", header_value, 1);
        } else if (!strcasecmp(header_name, "CONTENT-LENGTH")) {
            setenv("CONTENT_LENGTH", header_value, 1);
        } else if (!strcasecmp(header_name, "USER-AGENT")) {
            setenv("HTTP_USER_AGENT", header_value, 1);
        } else if (!strcasecmp(header_name, "ACCEPT-COOKIE")) {
            setenv("HTTP_COOKIE", header_value, 1);
        } else if (!strcasecmp(header_name, "ACCEPT-CHARSET")) {
            setenv("HTTP_ACCEPT_CHARSET", header_value, 1);
        } else if (!strcasecmp(header_name, "HOST")) {
            setenv("HTTP_HOST", header_value, 1);
        } else if (!strcasecmp(header_name, "CONTENT-TYPE")) {
            setenv("CONTENT_TYPE", header_value, 1);
        }
    }

    char* tokenList[BUF_SIZE];
    char request_uri_copy[BUF_SIZE];
    strcpy(request_uri_copy, request->http_uri);
    char* token = strtok(request_uri_copy, "?");
    tokenList[0] = token;
    token = strtok(NULL, "");

    //Remote address
    char addr[20];
    sprintf(addr, "%d", connFd);
    setenv("SERVER_SOFTWARE", "icws", 1);
    setenv("GATEWAY_INTERFACE", "CGI/1.1", 1);
    setenv("REQUEST_METHOD", request->http_method, 1);
    setenv("REQUEST_URI", request->http_uri, 1);
    setenv("SERVER_PROTOCOL", "HTTP/1.1", 1);
    setenv("QUERY_STRING", token ? token : "", 1);
    setenv("REMOTE_ADDR", addr, 1);
    setenv("PATH_INFO", tokenList[0], 1);
    setenv("SERVER_PORT", port, 1);

    CHECK("token");
}


int handle_cgi(Task *task) {
    CHECK("Handling CGI request");

    Request *request = task->request;
    int connFd = task->connFd;


    CHECK("Checking if the file exists");
    char* cgi_program_full_path = (char *)calloc(strlen(cgiProgram) + 3, sizeof(char)); // +3 for "./" and null terminator
    if (cgi_program_full_path == NULL) {
        send_500(connFd);
        return -1;
    }
    strcpy(cgi_program_full_path, "./");
    strcat(cgi_program_full_path, cgiProgram);


    printf("full path is %s\n", cgi_program_full_path);
    CHECK("create path");
    CHECK(cgi_program_full_path);
    if (access(cgi_program_full_path, R_OK | X_OK) < 0) {
        perror("access error");
        printf("Cannot access: %s", cgi_program_full_path);
        send_500(connFd);
        return -1;
    }

    int c2pFds[2]; /* Child to parent pipe */
    int p2cFds[2]; /* Parent to child pipe */

    if (pipe(c2pFds) < 0 || pipe(p2cFds) < 0) {
        printf("Failed to create pipe");
        send_500(connFd);
        return -1;
    }
    
    CHECK("Setting up the environment");
    set_up_cgi(task); // set up the env
    CHECK("SET the environment");
    int pid = fork();

    if (pid == 0) { /* Child - set up the conduit & run inferior cmd */
        CHECK("Child process");
        /* Wire pipe's incoming to child's stdin */
        /* First, close the unused direction. */
        if (close(p2cFds[1]) < 0) fail_exit("failed to close p2c[1]");
        if (p2cFds[0] != STDIN_FILENO) {
            if (dup2(p2cFds[0], STDIN_FILENO) < 0)
                fail_exit("dup2 stdin failed.");
            if (close(p2cFds[0]) < 0)
                fail_exit("close p2c[0] failed.");
        }

        /* Wire child's stdout to pipe's outgoing */
        /* But first, close the unused direction */
        if (close(c2pFds[0]) < 0) fail_exit("failed to close c2p[0]");
        if (c2pFds[1] != STDOUT_FILENO) {
            if (dup2(c2pFds[1], STDOUT_FILENO) < 0)
                fail_exit("dup2 stdin failed.");
            if (close(c2pFds[1]) < 0)
                fail_exit("close pipeFd[0] failed.");
        }

        char* inferiorArgv[] = {cgi_program_full_path, NULL};

        if (execv(inferiorArgv[0], inferiorArgv) < 0)
            perror("execv failed");

    }
    else { 
        free(cgi_program_full_path);
        
        if (close(c2pFds[1]) < 0) fail_exit("failed to close c2p[1]");
        if (close(p2cFds[0]) < 0) fail_exit("failed to close p2c[0]");

        // if (request->http_body != NULL) {
        //     char *body = request->http_body;
        //     size_t body_length = strlen(body);
        //     write_all(p2cFds[1], body, body_length);
        // }

        /* Close this end, done writing. */
        if (close(p2cFds[1]) < 0) fail_exit("close p2c[01] failed.");

        char result[BUF_SIZE + 1] = ""; // Initialize result buffer
        char buf[BUF_SIZE + 1]; // Buffer for reading from the pipe
        ssize_t numRead;

        /* Begin reading from the child */
        while ((numRead = read(c2pFds[0], buf, BUF_SIZE)) > 0) {
            printf("Parent saw %ld bytes from child...\n", numRead);
            buf[numRead] = '\0'; // Null-terminate the buffer
            strcat(result, buf); // Concatenate buffer contents to result
            // puts(buf); // Print buffer contents
        }

        size_t result_length = strlen(result);

        // Handle GET request
        if (strcmp(request->http_method, "GET") == 0) {
            write_all(connFd, result, result_length);
        }
        // Handle HEAD request
        else if (strcmp(request->http_method, "HEAD") == 0) {
            char *pos = strstr(result, "\r\n\r\n");
            if (pos != NULL) {
                size_t headers_length = pos - result + 4;
                write_all(connFd, result, headers_length);
            }
        }
        // Handle POST request
        else if (strcmp(request->http_method, "POST") == 0) {
            write_all(connFd, result, result_length);
        }

        /* Close this end, done reading. */
        if (close(c2pFds[0]) < 0) fail_exit("close c2p[01] failed.");

        /* Wait for child termination & reap */
        int status;

        if (waitpid(pid, &status, 0) < 0) fail_exit("waitpid failed.");
        
        printf("Child exited... parent's terminating as well.\n");
    }


}

void options_usage(int argc, char **argv) {

    struct option longOptions[] = {
        {"port", required_argument, 0, 'p'},
        {"root", required_argument, 0, 'r'},
        {"numThreads", required_argument, 0, 'n'},
        {"timeout", required_argument, 0, 't'},
        {"cgiHandler", required_argument, 0, 'c'},
        {0, 0, 0, 0, 0}};

    int optionIndex = 0;
    int c;

    while ((c = getopt_long(argc, argv, "p:r:n:t:c:", longOptions, &optionIndex)) != -1) {
        switch (c)
        {
        case 'p':
            port = optarg;
            break;
        case 'r':
            rootFolder = optarg;
            break;
        case 'n':
            numThreads = atoi(optarg);

            break;
        case 't':
            timeout = atoi(optarg);
            break;
        case 'c':
            cgiProgram = optarg;
            break;
        default:
            abort();
        }
    }

    if (port == NULL || rootFolder == NULL || cgiProgram == NULL) {
        fprintf(stderr, "Usage: %s --port <portNum> --root <rootFolder> --numThreads <numThreads> --timeout <timeout> --cgiHandler <cgiProgram>\n", argv[0]);
        exit(1);
    }

    printf("my cgi-program is %s\n", cgiProgram); // checking if cgi program is correct

}


int main(int argc, char **argv) {

    options_usage(argc, argv); // parse cmd line arguments

    int listenFd = open_listenfd(port);

    pthread_t threads[numThreads];
    pthread_mutex_init(&mutexqueue, NULL);
    pthread_cond_init(&condqueue, NULL);

    int i;
    for (i = 0; i < numThreads; i++) {
        pthread_create(&threads[i], NULL, startThread, NULL);
    }
    if (listenFd < 0) {
        fprintf(stderr, "Failed to open listen socket on port %s\n", port);
        exit(2);
    }   

    while (1) {

        printf("waiting for connection\n");

        struct sockaddr_storage clientAddr;
        socklen_t clientLen = sizeof(clientAddr);
        int connFd = accept(listenFd, (struct sockaddr *)&clientAddr, &clientLen);

        if (connFd < 0) {
            perror("accept");
            continue;
        }

        char buf[BUF_SIZE]; 
        char line[BUF_SIZE];
        memset(buf, 0, BUF_SIZE);
        memset(line, 0, BUF_SIZE); 

        struct pollfd fds[1];  
        int ret;
        int num_read = 0;
        int success = 0;

        while(1) {

            fds[0].fd = connFd;
            fds[0].events = POLLIN;

            ret = poll(fds, 1, timeout * 1000);

            // printf("The value of red is %d\n", ret);

            if (ret == -1) {
                send_500(connFd);
                perror("poll");
                break;
            }

            if (!ret) {
                printf("timeout\n");    
                send_408(connFd);

                // close(connFd);
                break;
            }

            if (fds[0].revents & POLLIN) {
                int numRead = read(connFd, line, BUF_SIZE);
                if (numRead > 0) {
                    if (numRead + num_read > BUF_SIZE) {
                        send_400(connFd);
                    }
                    num_read += numRead;

                    strcat(buf, line);
                    if (strstr(line, "\r\n\r\n") != NULL) {
                        memset(line, '\0', BUF_SIZE);
                        success = 1;
                        break;
                    }
                    memset(line, '\0', BUF_SIZE);
                }
            }
        }

        if (success) {

            pthread_mutex_lock(&mutexqueue);

            CHECK("connection accpeted"); 
            Request *request = parse(buf, BUF_SIZE, connFd); // Parse request

            printf("request message is %s\n", buf);
            pthread_mutex_unlock(&mutexqueue);

            if (!request) {
                send_501(connFd); // Send 501 on parse failure
                close(connFd);
                return; 
            }

            Task *newtask = (Task *) malloc(sizeof(Task));
            newtask->connFd = connFd;
            newtask->rootFolder = rootFolder;
            newtask->request = request;
            submit(*newtask);
            CHECK("Task submitted to the queue");
        }

        else {
            close(connFd);
        }
    }

    for (i = 0; i < numThreads; i++) {
        if (pthread_join(threads[i], NULL) != 0) {
            perror("pthread_join");
        }
    }

    pthread_mutex_destroy(&mutexqueue);
    pthread_cond_destroy(&condqueue);
    return 0;

}




char *determine_mime_type(const char *ext) {
    if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0)
        return "text/html";
    if (strcmp(ext, "css") == 0)
        return "text/css";
    if (strcmp(ext, "js") == 0)
        return "application/javascript";
    if (strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0)
        return "image/jpeg";
    if (strcmp(ext, "png") == 0)
        return "image/png";
    if (strcmp(ext, "txt") == 0)
        return "text/plain";
    return NULL; // Default MIME type or unknown
}


void serve_file(int connFd, char *filepath, char *mimeType, int headOnly) {

    struct stat sbuf;
    if (stat(filepath, &sbuf) < 0) {
        send_404(connFd);
        return;
    }

    char lastModified[BUF_SIZE];
    strftime(lastModified, sizeof(lastModified), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&sbuf.st_mtime));

    // Get the current server date
    char serverDate[BUF_SIZE];
    time_t now = time(0);
    strftime(serverDate, sizeof(serverDate), "%a, %d %b %Y %H:%M:%S GMT", gmtime(&now));

    // Prepare the header
    char header[BUF_SIZE];
    snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "Last-Modified: %s\r\n"
             "Date: %s\r\n"
             "\r\n",
             mimeType, sbuf.st_size, lastModified, serverDate);

    write_all(connFd, header, strlen(header));

    if (!headOnly) {
        int fileFd = open(filepath, O_RDONLY);
        char buf[BUF_SIZE];
        ssize_t bytesRead;
        while ((bytesRead = read(fileFd, buf, sizeof(buf))) > 0) {
            write_all(connFd, buf, bytesRead);
        }
        close(fileFd);
    }
    // close(connFd);
}

void send_404(int connFd) {
    const char *response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    write_all(connFd, response, strlen(response));
}

void send_501(int connFd) {
    const char *response = "HTTP/1.1 501 Not Implemented\r\nContent-Length: 0\r\n\r\n";
    write_all(connFd, response, strlen(response));
}

void send_505(int connFd) {
    const char *response = "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 0\r\n\r\n";
    write_all(connFd, response, strlen(response));
}

void send_400(int connFd) {
    const char *response = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
    write_all(connFd, response, strlen(response));
}

void send_408(int connFd) {
    const char *response = "HTTP/1.1 408 Request Timeout\r\nContent-Length: 0\r\n\r\n";
    write_all(connFd, response, strlen(response));
}

void send_500(int connFd) {
    const char *response = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\n\r\n";
    write_all(connFd, response, strlen(response));
}
