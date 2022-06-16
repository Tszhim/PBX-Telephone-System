/*
 * "PBX" server module.
 * Manages interaction with a client telephone unit (TU).
 */
#include <stdlib.h>

#include "debug.h"
#include "pbx.h"
#include "server.h"
#include "csapp.h"

/*
 * This function takes a file descriptor used for comunicating between client and thread, and reads the full message.
 */
char* read_client_msg(int fd, TU *tu) {
    char static_buf;
    FILE *stream;
    char *output_buf;
    size_t len;
    stream = open_memstream(&output_buf, &len);
    if(stream == NULL) {
         return NULL;
    }

    // Perform read calls until end of arg string or EOF encountered.
    int bytes_read = read(fd, &static_buf, 1);
    while(1) {
        // EOF encountered, terminate thread.
        if(bytes_read == 0) {
            fclose(stream);
            free(output_buf);
            return NULL;
        }
        // Error.
        else if(bytes_read == -1) {
            fclose(stream);
            free(output_buf);
            return NULL;
        }
        // Push buffered contents to dynamic buffer or exit if \r\n encountered.
        else {
            // Encountered \r.
            if(static_buf == '\r') {
                bytes_read = read(fd, &static_buf, 1);
                //Error.
                if(bytes_read == -1) {
                    fclose(stream);
                    free(output_buf);
                    return NULL;
                }
                // Encountered \n, exit.
                else if(static_buf == '\n') {
                    break;
                }
                // Not the end, push characters into dynamic buffer.
                else {
                    fprintf(stream, "%c", '\r');
                    fprintf(stream, "%c", static_buf);
                }
            }
            // Otherwise push to dynamic buffer.
            else {
                fprintf(stream, "%c", static_buf);
            }
        }
        bytes_read = read(fd, &static_buf, 1);
   }
   fprintf(stream, "%s", "\0");
   fclose(stream);
   return output_buf;
}

/*
 * Thread function for the thread that handles interaction with a client TU.
 * This is called after a network connection has been made via the main server
 * thread and a new thread has been created to handle the connection.
 */
void *pbx_client_service(void *arg) {
    // Fetch file descriptor for communication with the client, then detatch the thread.
    int connfd = *((int *)arg);
    free(arg);
    Pthread_detach(pthread_self());

    // Register with the PBX module.
    TU* tu;
    if(!(tu = tu_init(connfd))){
        close(connfd);
        return NULL;
    }
    if(pbx_register(pbx, tu, connfd) == -1) {
        close(connfd);
        return NULL;
    }

    // Enter service loop.
    while(1) {
        // Check # of args.
        int argc = 0;
        char* arg2_start = NULL;
        char* client_msg = read_client_msg(connfd, tu);

        // Successful read from client.
        if(client_msg) {
            // Iterate through read string.
            char* curr = client_msg;
            while(*curr) {
                // Maximum number of arguments is 2, any more is invalid.
                if(argc == 2) {
                    break;
                }
                // Check if command is chat, in which case spaces should not be treated as a new argument.
                else if(argc == 1 && strcmp(client_msg, "chat") == 0) {
                    break;
                }
                // Otherwise treat spaces normally (new arg).
                else if(*curr == ' ') {
                    *curr = '\0';
                    argc++;
                    arg2_start = curr + 1;
                }
                curr++;
            }
            argc++;

            // Commands are pickup, hangup, dial #, and chat str. Max # of args is 2.
            char* argv[argc];
            if(argc == 1) {
                argv[0] = strdup(client_msg);
            }
            else if(argc == 2) {
                argv[0] = strdup(client_msg);
                argv[1] = strdup(arg2_start);
            }

            // Check which case the command falls into and execute appropriate one.
            if(argc == 1 && strcmp(argv[0], "pickup") == 0) {
                tu_pickup(tu);
                debug("Picked up.");
            }
            else if(argc == 1 && strcmp(argv[0], "hangup") == 0){
                tu_hangup(tu);
                debug("Hanged up.");
            }
            else if(argc == 2 && strcmp(argv[0], "dial") == 0) {
                int ext = atoi(argv[1]);
                pbx_dial(pbx, tu, ext);
            }
            else if(strcmp(argv[0], "chat") == 0) {
                if(argc == 1) {
                    tu_chat(tu, "");
                }
                else {
                    tu_chat(tu, argv[1]);
                }
                debug("Sent chat message.");
            }

            // Free dynamic buffer and arguments that were strdup'd.
            free(client_msg);
            if(argc == 1) {
                free(argv[0]);
            }
            else if(argc == 2) {
                free(argv[0]);
                free(argv[1]);
            }

            // If none of the cases, then do nothing.
        }
        // Failed to read from client or EOF encountered, exit loop.
        else {
            break;
        }
    }
    // Close file descriptor when finished.
    close(connfd);
    pbx_unregister(pbx, tu);
    return NULL;
}