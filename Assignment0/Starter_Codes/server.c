#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <errno.h>

#define QUEUE_LENGTH 10
#define RECV_BUFFER_SIZE 2048

/* TODO: server()
 * Open socket and wait for client to connect
 * Print received message to stdout
 * Return 0 on success, non-zero on failure
*/
int server(char *server_port)
{
    char buf[RECV_BUFFER_SIZE];
    int status;
    struct addrinfo hints;
    struct addrinfo *servinfo, *p;  // will point to the results
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((status = getaddrinfo(NULL, server_port, &hints, &servinfo)) != 0) {
        fprintf(stderr,"[SERVER ERROR] getaddrinfo error: %s\n", gai_strerror(status));
        return 1;
    }
    int sockfd;
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            continue;
        }
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) { // bind socket to the post
            close(sockfd); // bind fail, release socket
            continue;
        }
        break;
    }

    if (p == NULL)  {
        perror("[SERVER ERROR] : failed to connect\n");
        return 2;
    }

    if (listen(sockfd, QUEUE_LENGTH) == -1) {
        perror("[SERVER ERROR] : failed to listen\n");
        return 3;
    }
    freeaddrinfo(servinfo); // all done with this structure
    // loop to receive
    while (1)
    {
        struct sockaddr_storage client_addr;
        int add_size = sizeof(client_addr);
        int newfd = accept(sockfd, (struct sockaddr *)&client_addr, &add_size);
        if (newfd == -1)
            continue;
        while (1)
        {
            memset(buf, 0, RECV_BUFFER_SIZE);
            int bytesRecv = recv(newfd, buf, RECV_BUFFER_SIZE, 0);
            if (bytesRecv < 0)
            {
                perror("[SERVER ERROR] failed to receive\n");
                return 4;
            }
            else if (bytesRecv == 0) // recv EOF
            {
                // fprintf(stdout, "[SERVER SUCCESS] read EOF\n");
                // fflush(stdout);
                break;
            }
            else
            {
                fwrite(buf, sizeof(char), bytesRecv, stdout);
                fflush(stdout);
            }
        }
    }
    
    return 0;
}


/*
 * main():
 * Parse command-line arguments and call server function
*/
int main(int argc, char **argv) {
  char *server_port;

  if (argc != 2) {
    fprintf(stderr, "Usage: ./server-c [server port]\n");
    exit(EXIT_FAILURE);
  }

  server_port = argv[1];
  return server(server_port);
}
