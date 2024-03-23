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
#include <fcntl.h>
#include "pcsa_net.h" // Assume this is correctly implemented
#include <pthread.h>
#include "parse.h"
#include <time.h> // For formatting dates
#include <poll.h>
#include <stdbool.h>

typedef struct Task
{
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

#define BUF_SIZE 8192
#define MAX 256

Task taskqueue[MAX];
int taskcount = 0;

#define PASS(msg) printf("PASS: %s\n", msg); 

pthread_mutex_t mutexqueue = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condqueue = PTHREAD_COND_INITIALIZER;

void submit(Task task) {
    // PASS("task submitting to the queue");
    pthread_mutex_lock(&mutexqueue);
    taskqueue[taskcount++] = task;
    task = taskqueue[0];
    pthread_mutex_unlock(&mutexqueue);
    pthread_cond_signal(&condqueue);
    // PASS("Task submitted to the queue");
}

void *startThread(void *args) {
    while (1) {
        // PASS("Thread is waiting for a task"); 
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
        // PASS("Thread is executing a task"); 
        execute(&task); // execute the task
        // PASS("Thread is done executing a task"); 
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

    char *ext = strrchr(request->http_uri, '.');
    ext = ext ? ext + 1 : ""; // Extract file extension
    char *mimeType = determine_mime_type(ext);
    mimeType = mimeType ? mimeType : "application/octet-stream"; // Default to binary stream if unknown

    if (strcmp(request->http_version, "HTTP/1.1")) {
        send_505(connFd); // HTTP Version Not Supported
    }

    else if (strcasecmp(request->http_method, "GET") != 0 && strcasecmp(request->http_method, "HEAD") != 0) {
        send_501(connFd); // Not Implemented
    }

    else if (strcasecmp(request->http_method, "GET") == 0 ||
             strcasecmp(request->http_method, "HEAD") == 0) {
        char filepath[BUF_SIZE];
        snprintf(filepath, sizeof(filepath), "%s%s", rootFolder, request->http_uri);
        serve_file(connFd, filepath, mimeType, strcasecmp(request->http_method, "HEAD") == 0);
    }
    else {
        send_400(connFd); // Bad Request
    }
    if (request->headers) {
        free(request->headers); // Free allocated headers
    }
    free(request); // Free the request structure
    printf("close connection after checking request\n"); 
    close(connFd);
}


void options_usage(int argc, char **argv) {

    struct option longOptions[] = {
        {"port", required_argument, 0, 'p'},
        {"root", required_argument, 0, 'r'},
        {"numThreads", required_argument, 0, 'n'},
        {"timeout", required_argument, 0, 't'},
        {0, 0, 0, 0}};

    int optionIndex = 0;
    int c;

    while ((c = getopt_long(argc, argv, "p:r:n:t:", longOptions, &optionIndex)) != -1) {
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
        default:
            abort();
        }
    }

    if (port == NULL || rootFolder == NULL) {
        fprintf(stderr, "Usage: %s --port <portNum> --root <rootFolder>\n", argv[0]);
        exit(1);
    }
}


int main(int argc, char **argv) {

    if (argc < 5) {
        fprintf(stderr, "Usage: %s --port <portNum> --root <rootFolder> --numThreads <numThreads> --timeout <timeout>\n", argv[0]);
        exit(1);
    }
    options_usage(argc, argv);



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

            printf("The value of red is %d\n", ret);

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
            Request *request = parse(buf, BUF_SIZE, connFd); // Parse request
            printf("request message is %s\n", buf);
 
            pthread_mutex_unlock(&mutexqueue);

            if (!request) {
                send_501(connFd); // Send 501 on parse failure
                close(connFd);
                continue;
            }

            Task *newtask = (Task *) malloc(sizeof(Task));
            newtask->connFd = connFd;
            newtask->rootFolder = rootFolder;
            newtask->request = request;
            submit(*newtask);
            PASS("Task submitted to the queue");

        }
        else{
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
