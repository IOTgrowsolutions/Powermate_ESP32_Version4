#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WebServer.h>

// Declare the server object as 'extern' so other files can access it
extern WebServer server;

// Initializes the primary web server and all its endpoints
void web_server_init();

// Handles incoming client requests in the main loop
void web_server_handle_client();

#endif // WEB_SERVER_H
