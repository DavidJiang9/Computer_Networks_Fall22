/*
 * transport.c 
 *
 * EN.601.414/614: HW#3 (STCP)
 *
 * This file implements the STCP layer that sits between the
 * mysocket and network layers. You are required to fill in the STCP
 * functionality in this file. 
 *
 */


#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <arpa/inet.h>
#include "mysock.h"
#include "stcp_api.h"
#include "transport.h"


enum { 
    CSTATE_ESTABLISHED, 
    AFTER_SENDING_SYN, 
    AFTER_RECEIVE_SYN,
    AFTER_SENDING_SYNACK,
    AFTER_RECEIVE_SYNACK,
    AFTER_SENDING_ACK,
    AFTER_RECEIVE_ACK,
    FIN_WAIT1,
    FIN_WAIT2,
    CLOSE_WAIT,
    LAST_ACK,
    CSTATE_CLOSED
};    /* obviously you should have more states */
#define MAX_WINDOW_SIZE 3072
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
/* this structure is global to a mysocket descriptor */
typedef struct
{
    bool_t done;    /* TRUE once connection is closed */

    int connection_state;   /* state of the connection (established, etc.) */
    tcp_seq initial_sequence_num;
    tcp_seq self_seq;  // my own sequence number
    tcp_seq next_seq;  // next sequence number
    tcp_seq peer_seq;  //other's sequence number
    uint16_t advertised_window; //Receiver advertises how many bytes are left within its window
    uint16_t effective_window; //sender set sending window size
    uint16_t last_byte_sent;
    uint16_t last_byte_ack;
    uint16_t last_byte_receive;
    uint16_t last_byte_read;
    /* any other connection-wide global variables go here */
} context_t;


static void generate_initial_seq_num(context_t *ctx);
static void control_loop(mysocket_t sd, context_t *ctx);
bool sendPacket(mysocket_t sd, context_t *ctx, uint8_t flag, char *data, size_t data_len);

//send packet based on context
bool sendPacket(mysocket_t sd, context_t *ctx, uint8_t flag, char *data, size_t data_len){
    STCPHeader * packet = (STCPHeader *)calloc(1, sizeof(STCPHeader) + data_len);
    packet->th_flags = flag;
    if (flag == TH_SYN) {
        packet->th_seq = htonl(ctx->initial_sequence_num);
        ctx->self_seq = ctx->initial_sequence_num;
        printf("sending SYN, %d, %d, %d\n", ctx->self_seq, ctx->peer_seq, ctx->initial_sequence_num);
    }
    else if (flag == TH_ACK) {
        packet->th_seq = htonl(ctx->self_seq);
        packet->th_ack = htonl(ctx->peer_seq+1);
        ctx->next_seq = ctx->self_seq+1; // self change in case of not receiving packet
        printf("sending ack, %d, %d\n", ctx->self_seq, ctx->peer_seq+1);
    }
    else if (flag == (TH_SYN | TH_ACK)) {
        packet->th_seq = htonl(ctx->initial_sequence_num);
        packet->th_ack = htonl(ctx->peer_seq+1);
        printf("sending Synack, %d, %d\n", ctx->self_seq, ctx->peer_seq+1);
    }
    else if (flag == TH_FIN) {
        packet->th_seq = htonl(ctx->self_seq); 
        printf("sending fin, %d, %d\n", ctx->self_seq, ctx->peer_seq+1);
    }
    else if (flag == (TH_FIN | TH_ACK)) {
        packet->th_seq = htonl(ctx->next_seq);
        packet->th_ack = htonl(ctx->peer_seq+1);
        printf("sending finack, %d, %d\n", ctx->next_seq, ctx->peer_seq+1);
    }
    else if (flag == 0) {
        assert(data);
        assert(data_len);
        memcpy((void *)(packet + sizeof(STCPHeader)), data, data_len);
        packet->th_seq = htonl(ctx->self_seq);
        packet->th_ack = htonl(ctx->peer_seq+1);
        
        ctx->next_seq += data_len;
        printf("sending data, datalen%d, seq%d, ack%d, next%d\n", (int)data_len, ctx->self_seq, ctx->peer_seq, ctx->next_seq);
        ctx->self_seq = ctx->next_seq;
        ctx->last_byte_sent = ctx->next_seq;
    }
    
    packet->th_off = 4;
    ctx->effective_window = MIN(MAX_WINDOW_SIZE,ctx->advertised_window)-(ctx->last_byte_sent-ctx->last_byte_ack);
    packet->th_win = htons(ctx->advertised_window);

    // send the packet
    ssize_t sent = stcp_network_send(sd, (void *)packet, sizeof(STCPHeader), data, data_len, NULL);
    if (sent > 0) {
        free(packet);
        return true;
    }
    else {
        fprintf(stderr, "sendPacket failed\n");
        errno = ECONNREFUSED; // connection refused by a server error
        free(packet);
        free(ctx);
        return false;
    }
}
//change own context based on packet
bool waitForPacket(mysocket_t sd, context_t *ctx, uint8_t flag){
    STCPHeader * packet = (STCPHeader *)calloc(1, sizeof(STCPHeader) + STCP_MSS);
    unsigned int val = stcp_wait_for_event(sd, NETWORK_DATA, NULL);
    if(val == NETWORK_DATA){
        ssize_t recv = stcp_network_recv(sd, (void *)packet, sizeof(STCPHeader) + STCP_MSS);
    }
    
    if ((flag == TH_SYN) && (packet->th_flags == TH_SYN)) {
        ctx->self_seq = ctx->initial_sequence_num;
        ctx->peer_seq = ntohl(packet->th_seq); //next ack, x, need to +1
        ctx->advertised_window = ntohs(packet->th_win); //SYN sender's max window
        ctx->effective_window = MIN(MAX_WINDOW_SIZE,ctx->advertised_window)-(ctx->last_byte_sent-ctx->last_byte_ack);
        free(packet);
        printf("receiving syn, %d, %d\n", ctx->self_seq, ctx->peer_seq);
        return true;
    }
    else if ((flag == TH_ACK) && (packet->th_flags == TH_ACK)) {
        ctx->peer_seq = ntohl(packet->th_seq); //next data sequence will start from x+2
        ctx->self_seq = ntohl(packet->th_ack); //next seq, y+1
        ctx->advertised_window = ntohs(packet->th_win); //SYN ACK sender's max window
        ctx->effective_window = MIN(MAX_WINDOW_SIZE,ctx->advertised_window)-(ctx->last_byte_sent-ctx->last_byte_ack);
        free(packet);
        printf("receiving ack, %d, %d\n", ctx->self_seq, ctx->peer_seq);
        return true;
    }
    else if ((flag == (TH_SYN | TH_ACK)) && (packet->th_flags == (TH_SYN | TH_ACK))) { //SYN ACk
        ctx->peer_seq = ntohl(packet->th_seq); //next ack, y, need to +1
        ctx->self_seq = ntohl(packet->th_ack); //next seq, x+1
        ctx->advertised_window = ntohs(packet->th_win); //SYN receiver(SYN ACK sender)'s max window
        ctx->effective_window = MIN(MAX_WINDOW_SIZE,ctx->advertised_window)-(ctx->last_byte_sent-ctx->last_byte_ack);
        free(packet);
        printf("receiving synack, %d, %d\n", ctx->self_seq, ctx->peer_seq);
        return true;
    }
    else if ((flag == TH_FIN) && (packet->th_flags == TH_FIN)) {
        ctx->peer_seq = ntohl(packet->th_seq); //next ack, u, need to +1
        ctx->advertised_window = ntohs(packet->th_win); //FIN sender's max window
        ctx->effective_window = MIN(MAX_WINDOW_SIZE,ctx->advertised_window)-(ctx->last_byte_sent-ctx->last_byte_ack);
        free(packet);
        printf("have fin\n");
        //time to send ack
        return true;
    }
    else if ((flag == (TH_FIN | TH_ACK)) && (packet->th_flags == (TH_FIN | TH_ACK))) {
        ctx->peer_seq = ntohl(packet->th_seq); //next ack, w, need to +1
        ctx->self_seq = ntohl(packet->th_ack); //next seq, u+1
        ctx->advertised_window = ntohs(packet->th_win); //FIN ACK sender's max window
        ctx->effective_window = MIN(MAX_WINDOW_SIZE,ctx->advertised_window)-(ctx->last_byte_sent-ctx->last_byte_ack);
        printf("receiving finack, %d, %d\n", ctx->self_seq, ctx->peer_seq);
        free(packet);
        return true;
    }
    else {
        fprintf(stderr, "waitForPacket failed\n");
        errno = ECONNREFUSED;
        if(packet) free(packet);
        free(ctx);
        return false;
    }
}
bool handleDataFromApp(mysocket_t sd, context_t *ctx){
    size_t max_data_len = MIN(STCP_MSS, ctx->effective_window); //sender max window
    char data[STCP_MSS + sizeof(STCPHeader)];
    ssize_t data_len = stcp_app_recv(sd, data, max_data_len); 
    // data[data_len]='\0';
    printf("app sending filename %s %d\n",data, data_len);
    // data_len+=1;
    if (data_len > 0) { // if length of data is larger than zero
        if (sendPacket(sd, ctx, 0, data, data_len) == false) {
            fprintf(stderr, "[handleDataFromApp]: failed when sending data packet\n");
            return false;
        }
        
        if (waitForPacket(sd, ctx, TH_ACK) == false) {
            fprintf(stderr, "[handleDataFromApp]: failed when waiting for ACK packet\n");
            return false;
        }
    }
    else {
        fprintf(stderr, "[handleDataFromApp]: received data_len is zero\n");
        errno = ECONNREFUSED;
        free(ctx);
        return false;
    }
    return true;
}

/* handle data received from network */
bool handleDataFromNetwork(mysocket_t sd, context_t *ctx){
    size_t max_data_len = MIN(STCP_MSS, ctx->advertised_window); //receive
    char data[STCP_MSS + sizeof(STCPHeader)];
    ssize_t data_len = stcp_network_recv(sd, (void *)data, sizeof(STCPHeader) + max_data_len); // receive and update data_len
    ctx->peer_seq = ctx->peer_seq + (size_t)data_len - sizeof(STCPHeader); //ack
    ctx->last_byte_receive = ctx->peer_seq;
    if (data_len >= (ssize_t)sizeof(STCPHeader)) { // if length of data is larger or equal to size of STCPHeader
        STCPHeader *packet = (STCPHeader *)data;
        if (packet->th_flags == TH_FIN) {
            
            ctx->peer_seq = ntohl(packet->th_seq); //next ack, u, need to +1
            printf("receiving fin, %d, %d\n", ctx->self_seq, ctx->peer_seq+1);
            ctx->advertised_window = ntohs(packet->th_win); //FIN sender's max window
            ctx->effective_window = MIN(MAX_WINDOW_SIZE,ctx->advertised_window)-(ctx->last_byte_sent-ctx->last_byte_ack);

            if (sendPacket(sd, ctx, TH_ACK, NULL, 0) == false) { // Sending ACK
                fprintf(stderr, "[handleDataFromNetwork]: failed when sending ACK packet\n");
                return false;
            }
            ctx->connection_state = CLOSE_WAIT;
            printf("Connection fin: %d\n", ctx->connection_state);

            
            if (sendPacket(sd, ctx, (TH_FIN | TH_ACK), NULL, 0) == false) { // Sending FINACK
                fprintf(stderr, "[handleDataFromNetwork]: failed when sending FINACK packet\n");
                return false;
            }
            ctx->connection_state = LAST_ACK;
            printf("Connection fin: %d\n", ctx->connection_state);

            if (waitForPacket(sd, ctx, TH_ACK) == false) { // Waiting for ACK
                fprintf(stderr, "[handleDataFromNetwork]: failed when waiting for ACK packet\n");
                return false;
            }
            ctx->connection_state = CSTATE_CLOSED;
            printf("Connection fin: %d\n", ctx->connection_state);

            stcp_fin_received(sd);
            ctx->done = 1;
            return true;
        }
        else { // send data to app
            STCPHeader *packet = (STCPHeader *)data;
            printf("receiving data stat: %d %d %d\n", (int)data_len, (int)sizeof(STCPHeader), (int)packet->th_off);
            int segment_size = (size_t)data_len - sizeof(STCPHeader);
            printf("received data: %s\n",(char *)data + sizeof(STCPHeader));
            // bcopy((char *)data + sizeof(STCPHeader), network_segment, segment_size);
            for(int i = 0; i < segment_size; i++){
                char *network_segment = (char*)calloc(1, sizeof(char));
                bcopy((char *)data + sizeof(STCPHeader) + i, network_segment, 1);
                // printf("byte of received data: %s\n",network_segment);
                stcp_app_send(sd, network_segment, 1);
            }
            // if(network_segment[segment_size-1]!='\0'){
            //     network_segment[segment_size]='\0';
            //     segment_size+=1;
            // }
            // network_segment[(size_t)data_len - sizeof(STCPHeader)] = '\0';
            // printf("ddddddddddddddddddddddddddddddddddddddddd network %s\n",network_segment);
            // stcp_app_send(sd, network_segment, segment_size);
            ctx->last_byte_read = ctx->peer_seq;
            ctx->advertised_window = MAX_WINDOW_SIZE - (ctx->last_byte_receive - ctx->last_byte_read);
            if (sendPacket(sd, ctx, TH_ACK, NULL, 0) == false) { // Sending ACK
                fprintf(stderr, "[handleDataFromNetwork]: failed when sending ACK packet\n");
                return false;
            }
            printf("received packet, after sending ack\n");
        }
        return true;
    }
    else {
        fprintf(stderr, "[handleDataFromNetwork]: received data_len is zero\n");
        free(ctx);
        return false;
    }
    return true;
}
bool handleAppClose(mysocket_t sd, context_t *ctx){
    if (sendPacket(sd, ctx, TH_FIN, NULL, 0) == false) { // Sending FIN
        fprintf(stderr, "[handleAppClose]: failed when sending FINACK packet\n");
        return false;
    }
    ctx->connection_state = FIN_WAIT1;
    printf("Connection state: %d\n", ctx->connection_state);
    
    if (waitForPacket(sd, ctx, TH_ACK) == false) { // Waiting for ACK
        fprintf(stderr, "[handleAppClose]: failed when waiting for ACK packet\n");
        return false;
    }
    ctx->connection_state = FIN_WAIT2;
    printf("Connection state: %d\n", ctx->connection_state);

    if (waitForPacket(sd, ctx, (TH_FIN | TH_ACK)) == false) { // Waiting for FINACK
        fprintf(stderr, "[handleAppClose]: failed when waiting for FINACK packet\n");
        return false;
    }
    if (sendPacket(sd, ctx, TH_ACK, NULL, 0) == false) { // Sending ACK
        fprintf(stderr, "[handleAppClose]: failed when sending ACK packet\n");
        return false;
    }
    ctx->connection_state = CSTATE_CLOSED;
    printf("Connection state: %d\n", ctx->connection_state);
    stcp_fin_received(sd);
    ctx->done = 1;
    return true;
}

/* initialise the transport layer, and start the main loop, handling
 * any data from the peer or the application.  this function should not
 * return until the connection is closed.
 */
void transport_init(mysocket_t sd, bool_t is_active)
{
    context_t *ctx;

    ctx = (context_t *) calloc(1, sizeof(context_t));
    assert(ctx);

    generate_initial_seq_num(ctx);
    ctx->next_seq = ctx->initial_sequence_num + 1;
    ctx->last_byte_sent = 0;
    ctx->last_byte_ack = 0;
    ctx->last_byte_receive = 0;
    ctx->last_byte_read = 0;
    ctx->advertised_window = MAX_WINDOW_SIZE;
    ctx->effective_window = MAX_WINDOW_SIZE;
    /* XXX: you should send a SYN packet here if is_active, or wait for one
     * to arrive if !is_active.  after the handshake completes, unblock the
     * application with stcp_unblock_application(sd).  you may also use
     * this to communicate an error condition back to the application, e.g.
     * if connection fails; to do so, just set errno appropriately (e.g. to
     * ECONNREFUSED, etc.) before calling the function.
     */
    if (is_active) {
        // send syn packet
        if (sendPacket(sd, ctx, TH_SYN, NULL, 0) == false) {
            fprintf(stderr, "[transport_init]: failed when sending SYN packet\n");
            return;
        }
        ctx->connection_state = AFTER_SENDING_SYN;
        printf("Connection state: %d\n", ctx->connection_state);

        // wait for syn ack
        if (waitForPacket(sd, ctx, (TH_SYN | TH_ACK)) == false) {
            fprintf(stderr, "[transport_init]: failed when waiting for SYNACK packet\n");
            return;
        }
        ctx->connection_state = AFTER_RECEIVE_SYNACK;
        printf("Connection state: %d\n", ctx->connection_state);

        // send ack
        if (sendPacket(sd, ctx, TH_ACK, NULL, 0) == false) {
            fprintf(stderr, "[transport_init]: failed when sending ACK packet\n");
            return;
        }
        ctx->connection_state = AFTER_SENDING_ACK;
        printf("Connection state: %d\n", ctx->connection_state);
    } else {
        // wait for syn
        if (waitForPacket(sd, ctx, TH_SYN) == false) {
            fprintf(stderr, "[transport_init]: failed when waiting for SYN packet\n");
            return;
        }
        ctx->connection_state = AFTER_RECEIVE_SYN;
        printf("Connection state: %d\n", ctx->connection_state);

        // send syn ack
        if (sendPacket(sd, ctx, (TH_SYN | TH_ACK), NULL, 0) == false) {
            fprintf(stderr, "[transport_init]: failed when sending SYNACK packet\n");
            return;
        }
        ctx->connection_state = AFTER_SENDING_SYNACK;
        printf("Connection state: %d\n", ctx->connection_state);

        // wait for ack
        if (waitForPacket(sd, ctx, TH_ACK) == false) {
            fprintf(stderr, "[transport_init]: failed when waiting for ACK packet\n");
            return;
        }
        ctx->connection_state = AFTER_RECEIVE_ACK;
        printf("Connection state: %d\n", ctx->connection_state);
    }
    ctx->connection_state = CSTATE_ESTABLISHED;
    stcp_unblock_application(sd);
    control_loop(sd, ctx);

    /* do any cleanup here */
    if(ctx) free(ctx);
}


/* generate random initial sequence number for an STCP connection */
static void generate_initial_seq_num(context_t *ctx)
{
    assert(ctx);

#ifdef FIXED_INITNUM
    /* please don't change this! */
    ctx->initial_sequence_num = 1;
#else
    /* you have to fill this up */
    /*ctx->initial_sequence_num =;*/
    srand(time(NULL));
    ctx->initial_sequence_num = rand() % 256;
#endif
}


/* control_loop() is the main STCP loop; it repeatedly waits for one of the
 * following to happen:
 *   - incoming data from the peer
 *   - new data from the application (via mywrite())
 *   - the socket to be closed (via myclose())
 *   - a timeout
 */
static void control_loop(mysocket_t sd, context_t *ctx)
{
    assert(ctx);

    while (!ctx->done)
    {
        unsigned int event;
        // printf("another loop: %d\n", ctx->connection_state);
        /* see stcp_api.h or stcp_api.c for details of this function */
        /* XXX: you will need to change some of these arguments! */
        event = stcp_wait_for_event(sd, ANY_EVENT, NULL);

        //receive packet from layer above
        if (event & APP_DATA)
        {
            printf("Connection app: %d\n", ctx->connection_state);
            /* the application has requested that data be sent */
            /* see stcp_app_recv() */
            if (handleDataFromApp(sd, ctx) == false) {
                return;
            }
        }
        //receive packet from layer below
        if (event & NETWORK_DATA) {
            printf("Connection network: %d\n", ctx->connection_state);
            /* received data from STCP peer */
            if (handleDataFromNetwork(sd, ctx) == false) {
                return;
            }
        }
        // app calls myclose
        if (event & APP_CLOSE_REQUESTED) {
            printf("Connection close: %d\n", ctx->connection_state);
            if (handleAppClose(sd, ctx) == false) {
                return;
            }
        }
        /* etc. */
    }
}


/**********************************************************************/
/* our_dprintf
 *
 * Send a formatted message to stdout.
 * 
 * format               A printf-style format string.
 *
 * This function is equivalent to a printf, but may be
 * changed to log errors to a file if desired.
 *
 * Calls to this function are generated by the dprintf amd
 * dperror macros in transport.h
 */
void our_dprintf(const char *format,...)
{
    va_list argptr;
    char buffer[1024];

    assert(format);
    va_start(argptr, format);
    vsnprintf(buffer, sizeof(buffer), format, argptr);
    va_end(argptr);
    fputs(buffer, stdout);
    fflush(stdout);
}