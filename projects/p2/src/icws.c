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


// Function prototypes
void serve_file(int connFd, char *filepath, char *mimeType, int headOnly);
void send_404(int connFd);
void send_501(int connFd);
char *determine_mime_type(const char *uri);

// int main(int argc, char **argv) {
//     char *port = NULL;
//     char *rootFolder = NULL;
//     int optionIndex = 0;

//     struct option longOptions[] = {
//         {"port", required_argument, 0, 'p'},
//         {"root", required_argument, 0, 'r'},
//         {0, 0, 0, 0}
//     };

//     int c;
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

//         // Only handle GET or HEAD requests
//         if (strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0) {
//             char filepath[BUF_SIZE];
//             snprintf(filepath, sizeof(filepath), "%s%s", rootFolder, uri);

//             char *mimeType = determine_mime_type(uri); // Determine MIME type

//             serve_file(connFd, filepath, mimeType, strcmp(method, "HEAD") == 0);
//         } else {
//             send_501(connFd);
//         }

//         close(connFd);
//     }
//     // close(listenFd); // Add proper signal handling to reach this point for cleanup.
//     return 0;
// }

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "pcsa_net.h" // Assuming pcsa_net.h provides necessary networking functions
#include <pthread.h>
#include "parse.h" // Assuming parse.h provides the Request structure and parsing function

#include <time.h>

#define BUF_SIZE 8192

void serve_file(int connFd, char *filepath, char *mimeType, int headOnly);
void send_404(int connFd);
void send_501(int connFd);
char *determine_mime_type(const char *uri);

int main(int argc, char **argv) {
    char *port = NULL;
    char *rootFolder = NULL;
    struct option longOptions[] = {
        {"port", required_argument, 0, 'p'},
        {"root", required_argument, 0, 'r'},
        {0, 0, 0, 0}
    };
    int optionIndex = 0;

    int c;
    while ((c = getopt_long(argc, argv, "p:r:", longOptions, &optionIndex)) != -1) {
        switch (c) {
            case 'p':
                port = optarg;
                break;
            case 'r':
                rootFolder = optarg;
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
        socklen_t clientLen = sizeof(clientAddr);
        int connFd = accept(listenFd, (struct sockaddr *)&clientAddr, &clientLen);
        if (connFd < 0) {
            perror("accept");
            continue;
        }

        char buf[BUF_SIZE] = {0};
        ssize_t bytesRead = read(connFd, buf, sizeof(buf) - 1); // Read request
        if (bytesRead <= 0) {
            close(connFd);
            continue;
        }

        Request *request = parse(buf, bytesRead, connFd); // Parse request
        if (!request) {
            send_501(connFd); // Send 501 on parse failure
            close(connFd);
            continue;
        }

        // Determine MIME type based on the file extension in the URI
        char *ext = strrchr(request->http_uri, '.');
        ext = ext ? ext + 1 : ""; // Extract file extension
        char *mimeType = determine_mime_type(ext);
        mimeType = mimeType ? mimeType : "application/octet-stream"; // Default to binary stream if unknown

        if (strcasecmp(request->http_method, "GET") == 0 || strcasecmp(request->http_method, "HEAD") == 0) {
            char filepath[BUF_SIZE];
            snprintf(filepath, sizeof(filepath), "%s%s", rootFolder, request->http_uri);
            serve_file(connFd, filepath, mimeType, strcasecmp(request->http_method, "HEAD") == 0);
        } else {
            send_501(connFd); // Method Not Implemented
        }

        if (request->headers) {
            free(request->headers); // Free allocated headers
        }
        free(request); // Free the request structure
        close(connFd);
    }
    return 0;
}

char *determine_mime_type(const char *ext) {
    if (strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0) return "text/html";
    if (strcmp(ext, "css") == 0) return "text/css";
    if (strcmp(ext, "js") == 0) return "application/javascript";
    if (strcmp(ext, "jpg") == 0 || strcmp(ext, "jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, "png") == 0) return "image/png";
    if (strcmp(ext, "txt") == 0) return "text/plain";
    return NULL; // Default MIME type or unknown
}

void serve_file(int connFd, char *filepath, char *mimeType, int headOnly) {
    struct stat sbuf;
    if (stat(filepath, &sbuf) < 0) {
        send_404(connFd);
        return;
    }



    // char header[BUF_SIZE];
    // snprintf(header, sizeof(header), 
    //          "HTTP/1.1 200 OK\r\n"
    //          "Content-Type: %s\r\n"
    //          "Content-Length: %ld\r\n"
    //          "\r\n", 
    //          mimeType, sbuf.st_size);

    // write_all(connFd, header, strlen(header));
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
