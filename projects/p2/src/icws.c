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

#define BUF_SIZE 8192

// void serve_file(int connFd, char *filepath, char *mimeType, int headOnly);
// void send_404(int connFd);

// void send_501(int connFd);

// int main(int argc, char **argv) {
//     char *port = NULL;
//     char *rootFolder = NULL;
//     int optionIndex = 0;
//     int fd_in = open(argv[1], O_RDONLY);
//     char buf[8192];
//     int readRet = read(fd_in,buf,8192);
//     // Request *request = parse(buf,readRet,fd_in);
//     int c;

//     struct option longOptions[] = {
//         {"port", required_argument, 0, 'p'},
//         {"root", required_argument, 0, 'r'},
//         {0, 0, 0, 0}
//     };

//     while ((c = getopt_long(argc, argv, "p:r:", longOptions, &optionIndex)) != -1) {
//         switch (c) {
//             case 'p':
//                 port = optarg;
//                 break;
//             case 'r':
//                 rootFolder = optarg;
//                 break;
//             case '?':
//                 // getopt_long already printed an error message.
//                 break;
//             default:
//                 abort();
//         }
//     }

//     if (port == NULL || rootFolder == NULL) {
//         fprintf(stderr, "Usage: %s --port <portNum> --root <rootFolder>\n", argv[0]);
//         exit(1);
//     }

//     int listenFd = open_listenfd(port);
//     if (listenFd < 0) {
//         fprintf(stderr, "Failed to open listen socket on port %s\n", port);
//         exit(2);
//     }

//     printf("Listening on port %s, serving files from %s\n", port, rootFolder);

//     while (1) {
//         struct sockaddr_storage clientAddr;
//         socklen_t clientLen = sizeof(struct sockaddr_storage);
//         int connFd = accept(listenFd, (struct sockaddr *)&clientAddr, &clientLen);
//         if (connFd < 0) {
//             perror("accept");
//             continue;
//         }

//         char buf[BUF_SIZE];
//         if (read_line(connFd, buf, BUF_SIZE) <= 0) {
//             close(connFd);
//             continue;
//         }

//         // Parse the request line
//         char method[BUF_SIZE], uri[BUF_SIZE], version[BUF_SIZE];
//         sscanf(buf, "%s %s %s", method, uri, version);

//         // Only handle GET requests
//         if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) {
//     // Construct the full file path
//     char filepath[BUF_SIZE];
//     snprintf(filepath, sizeof(filepath), "%s%s", rootFolder, uri);

//     // Determine MIME type
//     char *mimeType = "application/octet-stream"; // Default MIME type
//     if (strstr(uri, ".html")) mimeType = "text/html";
//     else if (strstr(uri, ".jpg") || strstr(uri, ".jpeg")) mimeType = "image/jpeg";

//     serve_file(connFd, filepath, mimeType, strcmp(method, "HEAD") == 0);
//     } else {
//     // If method is neither GET nor HEAD, send 501 Not Implemented
//     send_501(connFd);
// }
// close(connFd);
// continue;
// }
// }

// Function prototypes
void serve_file(int connFd, char *filepath, char *mimeType, int headOnly);
void send_404(int connFd);
void send_501(int connFd);
char *determine_mime_type(const char *uri);

int main(int argc, char **argv) {
    char *port = NULL;
    char *rootFolder = NULL;
    int optionIndex = 0;

    struct option longOptions[] = {
        {"port", required_argument, 0, 'p'},
        {"root", required_argument, 0, 'r'},
        {0, 0, 0, 0}
    };

    int c;
    while ((c = getopt_long(argc, argv, "p:r:", longOptions, &optionIndex)) != -1) {
        switch (c) {
            case 'p':
                port = optarg;
                break;
            case 'r':
                rootFolder = optarg;
                break;
            case '?':
                // getopt_long already printed an error message.
                break;
            default:
                abort();
        }
    }

    if (port == NULL || rootFolder == NULL) {
        fprintf(stderr, "Usage: %s --port <portNum> --root <rootFolder>\n", argv[0]);
        exit(1);
    }

    int listenFd = open_listenfd(port);
    if (listenFd < 0) {
        fprintf(stderr, "Failed to open listen socket on port %s\n", port);
        exit(2);
    }

    printf("Listening on port %s, serving files from %s\n", port, rootFolder);

    while (1) {
        struct sockaddr_storage clientAddr;
        socklen_t clientLen = sizeof(struct sockaddr_storage);
        int connFd = accept(listenFd, (struct sockaddr *)&clientAddr, &clientLen);
        if (connFd < 0) {
            perror("accept");
            continue;
        }

        char buf[BUF_SIZE];
        if (read_line(connFd, buf, BUF_SIZE) <= 0) {
            close(connFd);
            continue;
        }

        // Parse the request line
        char method[BUF_SIZE], uri[BUF_SIZE], version[BUF_SIZE];
        sscanf(buf, "%s %s %s", method, uri, version);

        // Only handle GET or HEAD requests
        if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) {
            char filepath[BUF_SIZE];
            snprintf(filepath, sizeof(filepath), "%s%s", rootFolder, uri);

            char *mimeType = determine_mime_type(uri); // Determine MIME type

            serve_file(connFd, filepath, mimeType, strcmp(method, "HEAD") == 0);
        } else {
            send_501(connFd);
        }

        close(connFd);
    }
    // close(listenFd); // Add proper signal handling to reach this point for cleanup.
    return 0;
}

char *determine_mime_type(const char *uri) {
    char *mimeType = "application/octet-stream"; // Default MIME type
    if (strstr(uri, ".html") || strstr(uri, ".htm")) mimeType = "text/html";
    else if (strstr(uri, ".css")) mimeType = "text/css";
    else if (strstr(uri, ".js")) mimeType = "application/javascript";
    else if (strstr(uri, ".jpg") || strstr(uri, ".jpeg")) mimeType = "image/jpeg";
    else if (strstr(uri, ".png")) mimeType = "image/png";
    else if (strstr(uri, ".txt")) mimeType = "text/plain";
    // Extend with other MIME types as needed
    return mimeType;
}

// void serve_file(int connFd, char *filepath, char *mimeType, int headOnly) {
//     struct stat sbuf;
//     if (stat(filepath, &sbuf) < 0) { // File does not exist
//         send_404(connFd);
//         return;
//     }

//     int fileFd = open(filepath, O_RDONLY);
//     if (fileFd < 0) { // Failed to open file
//         send_404(connFd);
//         return;
//     }

//     char header[BUF_SIZE];
//     // tell the last modified time, extension, mime type, server date, server last modified, and content length
//     snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n", mimeType, sbuf.st_size);
//     write_all(connFd, header, strlen(header)); 

//     if (!headOnly) {
//         char buf[BUF_SIZE];
//         ssize_t bytesRead;
//         while ((bytesRead = read(fileFd, buf, sizeof(buf))) > 0) {
//             write_all(connFd, buf, bytesRead);
//         }
//     }

//     close(fileFd);
// }

void serve_file(int connFd, char *filepath, char *mimeType, int headOnly) {
    struct stat sbuf; // File stats
    if (stat(filepath, &sbuf) < 0) { // File does not exist
        send_404(connFd);
        return;
    }

    // Get the last modified time from the file stats
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

    // Write body if not a HEAD request
    if (!headOnly) {
        int fileFd = open(filepath, O_RDONLY);
        if (fileFd < 0) { // Failed to open file
            send_404(connFd);
            return;
        }
        char buf[BUF_SIZE];
        ssize_t bytesRead;
        while ((bytesRead = read(fileFd, buf, sizeof(buf))) > 0) {
            write_all(connFd, buf, bytesRead);
        }
        close(fileFd);
    }
}


void send_404(int connFd) {
    char *response = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    write_all(connFd, response, strlen(response));
}

void send_501(int connFd) {
    char *response = "HTTP/1.1 501 Not Implemented\r\nContent-Length: 0\r\n\r\n";
    write_all(connFd, response, strlen(response));
}

