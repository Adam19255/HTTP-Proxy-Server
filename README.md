# ProxyServer - Multithreaded HTTP Proxy Server in C

ProxyServer is a robust, multithreaded HTTP proxy server implemented in C. It handles client requests, applies filtering rules, and forwards requests to destination servers.

## Features

- Multithreaded design using a thread pool for efficient request handling
- IP and hostname-based filtering
- HTTP/1.0 and HTTP/1.1 support
- Error handling with appropriate HTTP status codes
- Connection management (close connections after each request)

## Usage

Compile the program: gcc -o proxyServer proxyServer.c threadpool.c -lpthread

Run the program: ./proxyServer <port> <pool-size> <max-number-of-request> <filter>

- `<port>`: Port number on which the proxy server will listen
- `<pool-size>`: Number of threads in the thread pool
- `<max-number-of-request>`: Maximum number of requests the server will handle before shutting down
- `<filter>`: Path to the filter file containing IP addresses and hostnames to block

## Filter File Format

The filter file should contain one rule per line. Rules can be:

- IP addresses (e.g., `192.168.1.1`)
- IP address ranges (e.g., `192.168.1.0/24`)
- Hostnames (e.g., `example.com`)

## How It Works

1. Initializes a thread pool and opens the filter file
2. Sets up a socket to listen for incoming connections
3. Accepts client connections and dispatches them to the thread pool
4. For each client request:
   - Parses the HTTP request
   - Checks if the destination is allowed based on the filter rules
   - If allowed, forwards the request to the destination server
   - Receives the response from the destination server
   - Sends the response back to the client
5. Handles various error conditions with appropriate HTTP status codes
6. Closes connections after each request

## Error Handling

The server handles various error conditions, including:

- 400 Bad Request
- 403 Forbidden (for filtered addresses)
- 404 Not Found
- 500 Internal Server Error
- 501 Not Implemented (for unsupported HTTP methods)

## Dependencies

- Standard C libraries
- POSIX threads (pthread)
- Custom threadpool implementation (threadpool.c and threadpool.h)

## Limitations

- Only supports HTTP (not HTTPS)
- Only supports GET requests
