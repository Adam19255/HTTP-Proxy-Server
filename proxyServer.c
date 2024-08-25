#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <time.h>
#include <ctype.h>
#include "threadpool.h"

#define MAX_REQUEST_SIZE 2048
#define MAX_FILTER_SIZE 128
#define MAX_HOST_SIZE 1024

// Define structures for thread arguments and filter data
typedef struct {
    int client_socket;
    FILE *filterFile;
    int port;
} thread_args;

void displayErrorMessage(int client_socket, int error_num, int message, int status){
    const char* errorMessages[5] = {
            "Bad Request.",
            "Access denied.",
            "File not found.",
            "Some server side error.",
            "Method is not supported."
    };

    const char* statusMessages[5] = {
            "Bad Request",
            "Forbidden",
            "Not Found",
            "Internal Server Error",
            "Not supported"
    };

    // Get the current time
    time_t current_time;
    time(&current_time);

    // Format the time as shown in the files
    char formatted_time[256] = {0};
    struct tm* time_info = gmtime(&current_time);
    strftime(formatted_time, sizeof(formatted_time), "%a, %d %b %Y %H:%M:%S GMT", time_info);

    // Building the body of the response
    char body[512] = {0};
    snprintf(body, sizeof(body),
             "<HTML><HEAD><TITLE>%d %s</TITLE></HEAD>\r\n<BODY><H4>%d %s</H4>\r\n%s\r\n</BODY></HTML>",
             error_num, statusMessages[status], error_num, statusMessages[status], errorMessages[message]);

    // Building the headers of the response
    int content_length = strlen(body);
    char header[1024] = {0};
    snprintf(header, sizeof(header), "HTTP/1.1 %d %s\r\nServer: webserver/1.0\r\nDate: %s\r\n"
                                     "Content-Type: text/html\r\nContent-Length: %d\r\nConnection: close",
             error_num, statusMessages[status], formatted_time, content_length);

    // Combine both strings
    char error_response[2048] = {0};
    snprintf(error_response, sizeof(error_response), "%s\r\n\r\n%s", header, body);

    // Send the error_response to the client
    write(client_socket, error_response, strlen(error_response));
}

// Function to check the IP match of the filter rule
int isIPMatching(const char *givenIP, const char *filterIP, int maskLength) {
    // Convert IP addresses to numerical representations (in network byte order)
    uint32_t givenIPNumeric = ntohl(inet_addr(givenIP));
    uint32_t filterIPNumeric = ntohl(inet_addr(filterIP));

    // Create a mask based on the specified mask length
    uint32_t mask = 0xFFFFFFFF << (32 - maskLength);

    // Apply the mask to both IP addresses
    uint32_t maskedGivenIP = givenIPNumeric & mask;
    uint32_t maskedFilterIP = filterIPNumeric & mask;

    // Check if the masked IP addresses match
    return (maskedGivenIP == maskedFilterIP);
}

// Function to check if an address matches a filter rule
int isFiltered(const char *ip, const char *host, FILE *filterFile) {
    char filterRule[2048] = {0};

    // Setting the pointer to the start of the file
    if (fseek(filterFile, 0, SEEK_SET) != 0) {
        perror("file pointer\n");
        return 2; // Problem
    }

    // While we have more address in the file continue checking
    while (fgets(filterRule, sizeof(filterRule), filterFile) != NULL) {
        // Remove newline character at the end
        filterRule[strcspn(filterRule, "\r\n")] = '\0';

        // Check if the filter rule is an IP
        if (isdigit(*filterRule)) { // This is an IP address filter rule
            int maskLength = 32;
            // Separating the address from the mask length
            char* ip_token = strtok(filterRule, "/");
            char tempIP[128] = {0};
            strcpy(tempIP, ip_token);
            ip_token = strtok(NULL, "");
            if (ip_token != NULL){
                maskLength = atoi(ip_token);
            }

            if (isIPMatching(ip, tempIP, maskLength)) {
                return 1; // Filtered
            }
        } else { // This is a host filter rule
            if (strcmp(host, filterRule) == 0) { // Host matches the filter rule
                return 1; // Filtered
            }
        }
    }
    return 0; // IP address is not filtered
}

int receiveResponse(int server_sock, int client_sock){
    unsigned char *response = (unsigned char *)malloc(1024);
    while (1){
        // Read the response from the server
        ssize_t bytes_received = read(server_sock, response, 1024);
        if (bytes_received <= 0) {
            break;
        }
        // Write the response to the client
        ssize_t bytes_written = write(client_sock, response, bytes_received);
        if (bytes_written < 0) {
            free(response);
            return 1;
        }
    }
    free(response);
    return 0;
}

// Function to handle individual client requests
void handle_client(thread_args* args) {
    // Extract client socket and filter from thread arguments
    int client_socket = args->client_socket;
    // Save the filterFile pointer into a variable
    FILE *localFilterFile = args->filterFile;

    // Read HTTP request from the client
    char request[MAX_REQUEST_SIZE] = {0};
    while(1){
        char buffer[MAX_REQUEST_SIZE] = {0};
        ssize_t bytes_received = read(client_socket, buffer, sizeof(buffer));
        strcat(request, buffer);
        // We are only interested with the headers of the request, so we stop after reading them
        if (strstr(request, "\r\n\r\n") != NULL){
            break;
        }
        // The client socket was closed
        if (bytes_received == 0){
            free(args);
            return;
        }

        // Bad read
        if (bytes_received < 0) {
            perror("Request\n");
            // Server error, send 500 Some server error
            displayErrorMessage(client_socket, 500, 3, 3);
            close(client_socket);
            free(args);
            return;
        }
    }

    // Parse HTTP request and extract method, path, protocol, and host
    char method[16] = {0}, path[1024] = {0}, protocol[16] = {0}, host[MAX_HOST_SIZE] = {0};
    if (sscanf(request, "%15s %1023s %15s\r\n", method, path, protocol) != 3) {
        // Invalid request, send 400 Bad Request response
        displayErrorMessage(client_socket, 400, 0, 0);
        close(client_socket);
        free(args);
        return;
    }

    // Pointer to the start of the request
    char *request_ptr = request;
    // Iterate through lines in the request
    while ((request_ptr = strstr(request_ptr, "\n")) != NULL) {
        // Move past the newline character
        request_ptr++;
        // Check if the line starts with "Host:"
        if (strncmp(request_ptr, "Host:", 5) == 0) {
            // Extract the host from the line
            if (sscanf(request_ptr, "Host: %[^:\r\n]", host) != 1){
                // Invalid request, send 400 Bad Request response
                displayErrorMessage(client_socket, 400, 0, 0);
                close(client_socket);
                free(args);
                return;
            }
            break;
        }
    }

    // Check the http protocol version
    if (strcmp(protocol, "HTTP/1.1") != 0 && strcmp(protocol, "HTTP/1.0") != 0){
        displayErrorMessage(client_socket, 400, 0, 0);
        close(client_socket);
        free(args);
        return;
    }

    // Check if host exists
    if (strcmp(host, "") == 0){
        displayErrorMessage(client_socket, 400, 0, 0);
        close(client_socket);
        free(args);
        return;
    }

    // Check if the method is GET
    if (strcmp(method, "GET") != 0) {
        // Unsupported method, send 501 Not Implemented response
        displayErrorMessage(client_socket, 501, 4, 4);
        close(client_socket);
        free(args);
        return;
    }

    // Check if this host exist
    struct hostent* host_info = gethostbyname(host);
    if (host_info == NULL) {
        herror("gethostbyname failed\n");
        // Unable to resolve host, send 404 Not Found response
        displayErrorMessage(client_socket, 404, 2, 2);
        close(client_socket);
        free(args);
        return;
    }

    // Convert host address to ip address to check with the filterFile
    char ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, host_info->h_addr_list[0], ip, sizeof(ip));

    // Check if the IP address matches any filter rule
    int filtered = isFiltered(ip, host, localFilterFile);
    if (filtered == 1) {
        // Access denied, send 403 Forbidden response
        displayErrorMessage(client_socket, 403, 1, 1);
        close(client_socket);
        free(args);
        return;
    }
    else if (filtered == 2){
        // Server error, send 500 Some server error
        displayErrorMessage(client_socket, 500, 3, 3);
        close(client_socket);
        free(args);
        return;
    }

    // Create a socket to connect with the server
    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0){
        perror("Server_sock\n");
        displayErrorMessage(client_socket, 500, 3, 3);
        close(client_socket);
        free(args);
        return;
    }

    struct sockaddr_in sock_info;
    // Set its attributes to 0 to avoid undefined behavior
    memset(&sock_info, 0, sizeof(sock_info));
    // Set the socket's port
    sock_info.sin_port = htons(80);
    // Set the type of the address to be IPv4
    sock_info.sin_family = AF_INET;
    // Set the socket's IP
    sock_info.sin_addr = *((struct in_addr*)host_info->h_addr_list[0]);

    // Connect to the server
    if (connect(server_sock, (struct sockaddr*) &sock_info, sizeof(sock_info)) < 0){
        perror("Connect failed\n");
        displayErrorMessage(client_socket, 500, 3, 3);
        close(client_socket);
        close(server_sock);
        free(args);
        return;
    }

    // Check if "Connection" header exists and update its value to "close" or add the header
    char *connection_start = strstr(request, "Connection: keep-alive");
    if (connection_start != NULL) {
        // Pointer to what comes after "Connection: keep-alive"
        char *content_after_connection = connection_start + strlen("Connection: keep-alive");
        // Saving the content
        char temp[1024] = {0};
        strcpy(temp, content_after_connection);
        // Replace "keep-alive" with "close"
        strcpy(connection_start + 12, "close");
        // Appending the modified content after "Connection: close"
        strcat(request + 19, temp);
    } else {
        // Where "Connection: close" is not found, add it
        char *connection_end = strstr(request, "\r\n\r\n");
        strcpy(connection_end, "\r\nConnection: close\r\n\r\n");
    }

    // Ensure null terminator at the end
    size_t request_length = strlen(request);
    request[request_length] = '\0';

    // Send the HTTP request
    if (write(server_sock, request, strlen(request)) < 0) {
        perror("Request failed\n");
        displayErrorMessage(client_socket, 500, 3, 3);
        close(client_socket);
        close(server_sock);
        free(args);
        return;
    }

    // Receive the HTTP response
    if (receiveResponse(server_sock, client_socket)){
        perror("write to client failed\n");
        displayErrorMessage(client_socket, 500, 3, 3);
        close(server_sock);
        close(client_socket);
        free(args);
        return;
    }
    else{
        close(server_sock);
        close(client_socket);
        free(args);
    }
}


// Main function
int main(int argc, char* argv[]) {
    // Check for correct command line arguments
    if (argc != 5) {
        printf("Usage: proxyServer <port> <pool-size> <max-number-of-request> <filter>\n");
        exit(1);
    }

    // Parse command line arguments
    int port = atoi(argv[1]);
    int pool_size = atoi(argv[2]);
    int max_requests = atoi(argv[3]);
    char filter[MAX_FILTER_SIZE] = {0};
    strcpy(filter, argv[4]);

    if (port <= 0 || port > 65535 || pool_size <= 0 || max_requests <= 0){
        perror("Usage: proxyServer <port> <pool-size> <max-number-of-request> <filter>\n");
        exit(1);
    }

    // Initialize the thread pool
    threadpool* pool = create_threadpool(pool_size);
    if (pool == NULL){
        exit(1);
    }

    // Open the filter file
    FILE *filterFile = fopen(filter, "r");
    if (filterFile == NULL) {
        perror("Filter File open error\n");
        destroy_threadpool(pool);
        exit(1);
    }

    // Set up a socket to listen for incoming connections
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket\n");
        destroy_threadpool(pool);
        fclose(filterFile);
        exit(1);
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the socket to the specified port
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(struct sockaddr_in)) == -1) {
        perror("Bind\n");
        close(server_socket);
        destroy_threadpool(pool);
        fclose(filterFile);
        exit(1);
    }

    // Listen for incoming connections
    if (listen(server_socket, 10) == -1) {
        perror("Listen\n");
        close(server_socket);
        destroy_threadpool(pool);
        fclose(filterFile);
        exit(1);
    }

    // Accept and handle incoming connections
    int num_requests = 0;
    while (num_requests < max_requests) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_socket = accept(server_socket, (struct sockaddr*)&client_addr, &client_len);

        if (client_socket == -1) {
            perror("client_socket\n");
            continue;
        }

        // Create thread arguments and dispatch to the thread pool
        thread_args* args = (thread_args*)malloc(sizeof(thread_args));
        if (args == NULL){
            perror("Malloc\n");
            close(client_socket);
            continue;
        }
        args->client_socket = client_socket;
        args->port = port;
        args->filterFile = filterFile;

        dispatch(pool, (dispatch_fn)handle_client, args);

        num_requests++;
    }
    destroy_threadpool(pool);
    close(server_socket);
    fclose(filterFile);

    return 0;
}