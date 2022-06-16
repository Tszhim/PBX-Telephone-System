#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "pbx.h"
#include "server.h"
#include "debug.h"
#include "csapp.h"

static void terminate(int status);

/*
 * Signal handler for SIGHUP, which will set the global_flag to indicate termination should begin.
 */
void SIGHUP_handler(int sig) {
    terminate(EXIT_SUCCESS);
}

/*
 * "PBX" telephone exchange simulation.
 *
 * Usage: pbx <port>
 */
int main(int argc, char* argv[]){
    char* port;
    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    // Option processing should be performed here.
    // Option '-p <port>' is required in order to specify the port number
    // on which the server should listen.
    if(argc != 3 || strcmp(argv[1], "-p") != 0) {
        fprintf(stderr, "usage: bin/pbx -p <port>\n");
        exit(EXIT_FAILURE);
    }
    char* endptr;
    strtol(argv[2], &endptr, 10);
    if(endptr == argv[2] || *endptr != '\0') {
        fprintf(stderr, "usage: bin/pbx -p <port>\n");
        exit(EXIT_FAILURE);
    }
    port = argv[2];

    // Perform required initialization of the PBX module.
    debug("Initializing PBX...");
    pbx = pbx_init();
    if(!pbx) {
        exit(EXIT_FAILURE);
    }

    // TODO: Set up the server socket and enter a loop to accept connections
    // on this socket.  For each connection, a thread should be started to
    // run function pbx_client_service().  In addition, you should install
    // a SIGHUP handler, so that receipt of SIGHUP will perform a clean
    // shutdown of the server.
    Signal(SIGHUP, SIGHUP_handler);
    listenfd = Open_listenfd(port);
    debug("Listening for clients...");
    while(1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Pthread_create(&tid, NULL, pbx_client_service, connfdp);
        debug("Accepted new client.");
    }
    debug("An impossibility occured.");
    terminate(EXIT_FAILURE);
}

/*
 * Function called to cleanly shut down the server.
 */
static void terminate(int status) {
    debug("Shutting down PBX...");
    pbx_shutdown(pbx);
    debug("PBX server terminating");
    exit(status);
}
