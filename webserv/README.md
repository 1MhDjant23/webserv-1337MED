# Webserv

A robust, non-blocking HTTP/1.1 web server written in C++98.

## Overview
Webserv is a comprehensive implementation of an HTTP server, designed to handle multiple concurrent clients efficiently using I/O multiplexing. This project was built as part of the 42 Network curriculum (1337MED), focusing on understanding the underlying mechanisms of the HTTP protocol, socket programming, and server architecture.

## Features
- **Non-Blocking I/O**: Uses `poll()` to handle multiple connections simultaneously without blocking.
- **HTTP/1.1 Support**: Fully implements GET, POST, and DELETE methods.
- **Static Website Hosting**: Capable of serving HTML, CSS, JavaScript, and media files.
- **CGI Execution**: Supports running external scripts (like PHP or Python) via CGI.
- **Session Management**: Full implementation of Cookies and Session management for stateful user experiences.
- **Custom Configuration**: Powered by a flexible configuration file system (Nginx-style) to define routes, error pages, and server limits.
- **Robust Error Handling**: Informative error pages and status codes.

## Architecture
The project is built on a modular architecture to separate concerns and ensure stability:

### Server Core
The central engine of the application. It creates sockets, manages the main event loop, and orchestrates the flow of data between clients and the application logic. It handles connection timeouts, signals, and resource cleanup.

### Parsing Engine
A dedicated module responsible for dissecting raw HTTP requests. It validates headers, parses the body (including chunked transfer encoding), and prepares the data for processing.

### Configuration & CGI
The configuration module parses the `.conf` files to apply user-defined settings. The CGI module manages the environment and execution of dynamic scripts, piping input/output correctly between the server and the script.

## Installation & Usage

### Prerequisites
- C++ Compiler (g++)
- Make
- Linux or macOS

### Building
To compile the project, run:
```bash
make
```
### Running
Start the server by specifying a configuration file:
```bash
./webserv configs/conf.conf
```

## The Team

This project is the result of a collaborative effort:

- **mait-taj (me)** - *Server Core & Session Management*
  - **Core Architecture**: Designed and implemented the robust `SocketManager` to handle multiple listening sockets and concurrent client connections using `poll()` multiplexing.
  - **Virtual Hosting**: Implemented logic to handle multiple server blocks (Virtual Hosts), correctly routing requests based on IP/Port and Server Names.
  - **Session & Cookies**: Developed a complete Session Management system (Bonus) to enable stateful user interactions and secure cookie handling.
  - **Event Loop**: Orchestrated the main non-blocking event loop, ensuring efficient resource usage and connection timeout management.

- **abmahfou** - *HTTP Parsing & Data Handling*
  - **Request/Response Engine**: Built the complete parsing logic for HTTP methods (GET, POST, DELETE) and headers validation.
  - **Advanced Encodings**: Implemented parsing for **Chunked Transfer Encoding** and **Multipart/Form-Data**, enabling reliable file uploads and dynamic request bodies.
  - **Protocol Compliance**: Ensured strict adherence to HTTP/1.1 standards, handling edge cases and generating appropriate error status codes.

- **adbouras** - *Configuration & CGI*
  - **Config Parser**: Developed the flexible Nginx-style configuration parser (`.conf`), handling server blocks, locations, and routing rules.
  - **CGI Execution**: Implemented the CGI interface to securely execute external scripts (PHP, Python, Bash, etc.), managing environment variables and inter-process communication via pipes.
  - **Resource Handling**: Managed file system interactions, including auto-indexing, static file serving logic, and directory access controls.
