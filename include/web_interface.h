#ifndef WEB_INTERFACE_H
#define WEB_INTERFACE_H

// No need to include WebServer.h here anymore

// Declares the function that will add the calculator-specific routes
// to the main web server instance.
void register_calculator_endpoints();

#endif // WEB_INTERFACE_H
