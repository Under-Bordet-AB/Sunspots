# http_main
 - Offers the main http_init, http_accept and http_dispose functions
 - Initiates the TCP server and individual listener threads
 - Responsible for accepting connections and forwarding them to the listener threads

# http_worker
 - Threaded function that handles incoming connections.

# http_parser
 - Helper functions for parsing HTTP message formats.