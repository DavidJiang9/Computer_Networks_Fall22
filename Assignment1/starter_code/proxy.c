#include "proxy_parse.h"
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
#include <netinet/tcp.h>

#define MAX_CLIENTS_REQ 100
#define SEND_BUFFER_SIZE 8192
#define RECV_BUFFER_SIZE 8192
/* TODO: proxy()
 * Establish a socket connection to listen for incoming connections.
 * Accept each client request in a new process.
 * Parse header of request and get requested URL.
 * Get data from requested remote server.
 * Send data to the client
 * Return 0 on success, non-zero on failure
*/
char *badReqMsg   = "HTTP/1.0 400 Bad Request\r\nConnection: closed\r\n\r\n";

char *notFoundMsg = "<html><head>\r\b<title>404 Not Found</title>\r\n"\
              "</head><body>\r\n<h1>Not Found</h1>\r\n"\
              "</body></html>\r\n";

char *notImpMsg   = "HTTP/1.0 501 Not Implemented\r\nConnection: closed\r\n\r\n";
int proxy(char *proxy_port) {
    
    int status;
    struct addrinfo hints;
    struct addrinfo *servinfo, *p;  // will point to the results
    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
    hints.ai_flags = AI_PASSIVE; 

    if ((status = getaddrinfo(NULL, proxy_port, &hints, &servinfo)) != 0) {
        // fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        fprintf(stderr,"[PROXY ERROR] getaddrinfo error: %s\n", gai_strerror(status));
        fflush(stdout);
        return 1;
    }

    int proxysockfd;
    for(p = servinfo; p != NULL; p = p->ai_next) { // result of socket address information
        if ((proxysockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {  //int socket(int domain, int type, int protocol);
            continue;
        }
        if (bind(proxysockfd, p->ai_addr, p->ai_addrlen) == -1) { // no need to bind, connect checks to see if the socket is unbound, and will bind() it to an unused local port if necessary.
            close(proxysockfd);
            continue;
        }
        break;
    }

    if (p == NULL) {
        perror("[PROXY ERROR] : failed to connect\n");
        return 2;
    }
    listen(proxysockfd, MAX_CLIENTS_REQ);
    int newfd;
    while (1)
    {
        // fprintf(stdout, "%s\n","1111");
        struct sockaddr_storage client_addr;
        int add_size = sizeof(client_addr);
        newfd = accept(proxysockfd, (struct sockaddr *)&client_addr, (socklen_t * )&add_size); //client <-> proxy
        if (newfd == -1){
            // fprintf(stdout, "%s\n","1114");
            continue;
        }
            
        pid_t pid = fork();
        // fprintf(stdout, "%s\n","1112");
        if(pid==0) break;
        else close(newfd);
    }
    //child process
    // fprintf(stdout, "%s\n","1113");
    // close(proxysockfd);
    char buf[RECV_BUFFER_SIZE];
    int bytesRecv=0;
    while(1){
        int recved = recv(newfd, buf+bytesRecv, RECV_BUFFER_SIZE-bytesRecv, 0);
        if (recved < 0)
        {
            perror("[PROXY ERROR] failed to receive\n");
            return 4;
        }
        char* index = strstr(buf, "\r\n\r\n");
        if (index) {
            break;
        }
        bytesRecv += recved;
    }

    int rlen = strlen(buf);
    struct ParsedRequest *req = ParsedRequest_create();

    // 2.1 Gives invalid request output due to wrong format:
    if (ParsedRequest_parse(req, buf, rlen) < 0) {
        perror("[400] Bad Request");
        // char respond[RECV_BUFFER_SIZE];
        // strcpy(respond,"Error 400: Bad Request\n");
        printf("%s\n",badReqMsg);
        send(newfd, badReqMsg, strlen(badReqMsg), 0);
        return 6;
    }
    // printf("%s,%s\n",req->protocol,req->version);
    if(strstr(req->method, "GET")==NULL){
        perror("[501] Not Implemented");
        // char respond[RECV_BUFFER_SIZE];
        // strcpy(respond,"Error 501: Not Implemented\n");
        send(newfd, notImpMsg, strlen(notImpMsg), 0);
        return 7;
    }

    ParsedHeader_set(req, "Host", req->host);
    ParsedHeader_set(req, "Connection", "close");
    const char * host = req->host;
    // printf("%s\n",host);
    req->host = "";
    req->port = "80";
    int reqlen = ParsedRequest_totalLen(req);
    char *b = (char *)malloc(reqlen+1);
    if (ParsedRequest_unparse(req, b, reqlen) < 0) {
        perror("[400] Bad Request");
        // char respond[RECV_BUFFER_SIZE];
        // strcpy(respond,"Error 400: Bad Request\n");
        send(newfd, badReqMsg, strlen(badReqMsg), 0);
        return 6;
    }
    b[reqlen]='\0';
    
    
    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family = AF_UNSPEC;     // don't care IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
    // printf("%s,%s,%s\n",host,req->port,buf);

    if ((status = getaddrinfo(host, req->port, &hints, &servinfo)) != 0) {
        // fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        fprintf(stderr,"[PROXY ERROR] getaddrinfo error: %s\n", gai_strerror(status));
        fflush(stdout);
        return 1;
    }
    
    int pssockfd;
    for(p = servinfo; p != NULL; p = p->ai_next) { // result of socket address information
        if ((pssockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {  //int socket(int domain, int type, int protocol);
            continue;
        }
        if (connect(pssockfd, p->ai_addr, p->ai_addrlen) == -1) { // no need to bind, connect checks to see if the socket is unbound, and will bind() it to an unused local port if necessary.
            // printf("%s\n","no");
            close(pssockfd);
            continue;
        }
        break;
    }

    if (p == NULL) {
        perror("[PROXY ERROR] : failed to connect\n");
        return 2;
    }
    int res = send(pssockfd, buf, strlen(buf), 0);
    if (res == -1){
        perror("[PROXY ERROR] failed to send\n");
        return 4;
    }
    memset(&buf, 0, RECV_BUFFER_SIZE);
    while(recv(pssockfd, buf, RECV_BUFFER_SIZE-1, 0) != 0){
        // printf("%s\n","12");
        // fprintf(stdout, "%s", buf);
        b[strlen(buf)]='\0';
        send(newfd, buf, strlen(buf), 0);
        memset(&buf, 0, RECV_BUFFER_SIZE);
    }
    close(pssockfd);
    close(newfd);
    return 0;
}


int main(int argc, char * argv[]) {
  char *proxy_port;

  if (argc != 2) {
    fprintf(stderr, "Usage: ./proxy <port>\n");
    exit(EXIT_FAILURE);
  }

  proxy_port = argv[1];
  return proxy(proxy_port);
}
