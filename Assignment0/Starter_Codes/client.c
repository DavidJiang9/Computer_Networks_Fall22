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

#define SEND_BUFFER_SIZE 2048


/* TODO: client()
 * Open socket and send message from stdin.
 * Return 0 on success, non-zero on failure
*/
int client(char *server_ip, char *server_port)
{
    
    char buf[SEND_BUFFER_SIZE];
    int status;
    struct addrinfo hints;
    struct addrinfo *servinfo, *p;  // will point to the results
    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
    
    if ((status = getaddrinfo(server_ip, server_port, &hints, &servinfo)) != 0) {
        // fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        fprintf(stderr,"[CLIENT ERROR] getaddrinfo error: %s\n", gai_strerror(status));
        fflush(stdout);
        return 1;
    }
    
    int sockfd;
    for(p = servinfo; p != NULL; p = p->ai_next) { // result of socket address information
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {  //int socket(int domain, int type, int protocol);
            continue;
        }
        if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) { // no need to bind, connect checks to see if the socket is unbound, and will bind() it to an unused local port if necessary.
            close(sockfd);
            continue;
        }
        break;
    }

    if (p == NULL) {
        perror("[CLIENT ERROR] : failed to connect\n");
        return 2;
    }
    
    // loop to read stdin, and send
    while (1)
    {
        memset(buf, 0, SEND_BUFFER_SIZE);
        int bytesRead = fread(buf, sizeof(char), SEND_BUFFER_SIZE, stdin);
        if (bytesRead < 0)
        {
            perror("[CLIENT ERROR] failed to read stdin\n");
            return 3;
        }
        else if (bytesRead == 0) // read EOF
        {
            fprintf(stdout, "[CLIENT SUCCESS] client EOF\n");
            fflush(stdout);
            return 0;
        }
        else // partial send
        {
            int bytesSent = 0;
            int bytesLeft = bytesRead;
            int bytesPer;
            while (bytesSent < bytesRead) // maybe unabe to send bytes all in one time
            {
                bytesPer = send(sockfd, buf + bytesSent, bytesLeft, 0);
                if (bytesPer == -1){
                    perror("[CLIENT ERROR] failed to send\n");
                    return 4;
                }
                bytesSent += bytesPer;
                bytesLeft -= bytesPer;
                // printf("%s",p->ai_canonname);
                fprintf(stdout,"client sent %d bytes, %d bytes left\n",bytesSent, bytesLeft);
                fflush(stdout);
            }
        }
    }
    freeaddrinfo(servinfo); // free the linked-list
    close(sockfd);
    return 0;
}


/*
 * main()
 * Parse command-line arguments and call client function
*/
int main(int argc, char **argv) {
  char *server_ip;
  char *server_port;

  if (argc != 3) {
    fprintf(stderr, "Usage: ./client-c [server IP] [server port] < [message]\n");
    exit(EXIT_FAILURE);
  }

  server_ip = argv[1];
  server_port = argv[2];
  return client(server_ip, server_port);
}
