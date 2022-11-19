/**********************************************************************
 * file:  sr_router.c
 *
 * Description:
 *
 * This file contains all the functions that interact directly
 * with the routing table, as well as the main entry method
 * for routing.
 *
 **********************************************************************/

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include "sr_if.h"
#include "sr_rt.h"
#include "sr_router.h"
#include "sr_protocol.h"
#include "sr_arpcache.h"
#include "sr_utils.h"

/*---------------------------------------------------------------------
 * Method: sr_init(void)
 * Scope:  Global
 *
 * Initialize the routing subsystem
 *
 *---------------------------------------------------------------------*/

void sr_init(struct sr_instance* sr)
{
    /* REQUIRES */
    assert(sr);

    /* Initialize cache and cache cleanup thread */
    sr_arpcache_init(&(sr->cache));

    pthread_attr_init(&(sr->attr));
    pthread_attr_setdetachstate(&(sr->attr), PTHREAD_CREATE_JOINABLE);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_attr_setscope(&(sr->attr), PTHREAD_SCOPE_SYSTEM);
    pthread_t thread;

    pthread_create(&thread, &(sr->attr), sr_arpcache_timeout, sr);

    /* Add initialization code here! */

} /* -- sr_init -- */
void send_icmp_reply(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{
	sr_ethernet_hdr_t *oriEtherPacket = (sr_ethernet_hdr_t *)packet;
	struct sr_ip_hdr *oriIpPacket = (struct sr_ip_hdr *) (packet + sizeof(sr_ethernet_hdr_t));

	uint8_t * copy = (uint8_t *)malloc(len);
	memcpy(copy, packet, len); //must have
	sr_ethernet_hdr_t *etherPacket = (sr_ethernet_hdr_t *)copy;
	struct sr_ip_hdr *ipPacket = (struct sr_ip_hdr *) (copy + sizeof(sr_ethernet_hdr_t));
	sr_icmp_hdr_t * icmpPacket = (sr_icmp_hdr_t *) (copy + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
	struct sr_if *fromInterface = sr_get_interface(sr, interface);

	icmpPacket->icmp_type = 0;
	icmpPacket->icmp_code = 0;
	icmpPacket->icmp_sum = 0;
	icmpPacket->icmp_sum = cksum(icmpPacket, len - (sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t)));

	memcpy(ipPacket, oriIpPacket, sizeof(sr_ip_hdr_t));
	ipPacket->ip_len = htons(len - sizeof(sr_ethernet_hdr_t));
	ipPacket->ip_off = htons(IP_DF); //must have
	ipPacket->ip_ttl = 64;  //must not have
	ipPacket->ip_p = ip_protocol_icmp; //must not have
	ipPacket->ip_dst = oriIpPacket->ip_src;
	ipPacket->ip_src = fromInterface->ip;
	ipPacket->ip_sum = 0;
	ipPacket->ip_sum = cksum(ipPacket, sizeof(sr_ip_hdr_t));

	uint8_t * sourceMAC = (uint8_t *) fromInterface->addr;
	uint8_t * destMAC = (uint8_t *) oriEtherPacket->ether_shost;
	memcpy(etherPacket->ether_shost, sourceMAC, sizeof(uint8_t) * ETHER_ADDR_LEN);
	memcpy(etherPacket->ether_dhost, destMAC, sizeof(uint8_t) * ETHER_ADDR_LEN);
	// printf("%s\n","debug");
	// printf("%d %ld\n",len, sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_hdr_t)); //98.70
	// print_addr_eth(sourceMAC);
	// print_addr_eth(destMAC);
	etherPacket->ether_type = htons(ethertype_ip); //must have
	print_hdrs(copy, len); 
	sr_send_packet(sr, copy, len, interface);
	free(copy);
}
struct sr_rt *longestPrefix(struct sr_rt * entries, uint32_t ip){
	struct sr_rt* match = NULL;
	int longest = 0;
	while(entries != NULL){
		if((entries->mask.s_addr & ip) == (entries->mask.s_addr & entries->dest.s_addr)){
			uint32_t mask = entries->mask.s_addr;
			int len = 0;
			while(mask != 0){ // len of mask	
				len++; 
				mask = (mask & (mask-1)); 
			}
			if(len > longest){
				print_addr_ip_int(ip);
				print_addr_ip(entries->dest);
				printf("with matched length %d\n", len);
				longest = len;
				match = entries;
			}
		}
		entries = entries->next;
	}
	return match;
}
void send_icmp_error(struct sr_instance* sr, uint8_t type, uint8_t code, uint8_t * oriIPPacket, uint32_t targetIP, uint32_t sourceIP, unsigned int oriLen, uint8_t * oriEtherPacket, unsigned int oriEitherLen)
{
	unsigned int packetSize = sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t) + sizeof(sr_icmp_t3_hdr_t);
	uint8_t * out = malloc(packetSize);
	sr_ethernet_hdr_t *etherPacket = (sr_ethernet_hdr_t *)out;
	struct sr_ip_hdr *ipPacket = (struct sr_ip_hdr *) (out + sizeof(sr_ethernet_hdr_t));
	sr_icmp_t3_hdr_t * icmpPacket = (sr_icmp_t3_hdr_t *) (out + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
	memcpy(ipPacket, oriIPPacket, sizeof(sr_ip_hdr_t));

	icmpPacket->icmp_type = type;
	icmpPacket->icmp_code = code;

	memcpy(icmpPacket->data, oriIPPacket, ICMP_DATA_SIZE);
	icmpPacket->icmp_sum = 0;
	icmpPacket->icmp_sum = cksum(icmpPacket, packetSize - (sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t)));

	ipPacket->ip_len = htons(packetSize - sizeof(sr_ethernet_hdr_t));
	ipPacket->ip_off = htons(IP_DF);
	ipPacket->ip_ttl = 64;
	ipPacket->ip_p = ip_protocol_icmp; 
	ipPacket->ip_dst = targetIP;//send to sender,client

	//!!!!
	// sr_ethernet_hdr_t *oriEtherPacket = (sr_ethernet_hdr_t *)oriEtherPacket;
	// uint8_t * destMAC = (uint8_t *) oriEtherPacket->ether_shost;
	// memcpy(etherPacket->ether_dhost, destMAC, sizeof(uint8_t) * ETHER_ADDR_LEN);

	struct sr_rt *entry = longestPrefix(sr->routing_table, targetIP); //back!!!!
	if(entry != NULL){
		struct sr_if *outInterface = sr_get_interface(sr, entry->interface);
		ipPacket->ip_src = outInterface->ip;//back ,interface ip
		ipPacket->ip_sum = 0;
		ipPacket->ip_sum = cksum(ipPacket, sizeof(sr_ip_hdr_t));
		memcpy(etherPacket->ether_shost, outInterface->addr, sizeof(uint8_t) * ETHER_ADDR_LEN);
		etherPacket->ether_type = htons(ethertype_ip);
		printf("%s\n","found match in routing table, checking ARP cache");
		uint32_t nextIP = (uint32_t) entry->dest.s_addr; //!!!! definition
		struct sr_arpentry * arpEntry = sr_arpcache_lookup(&(sr->cache), nextIP);
		if (arpEntry != NULL) {
			printf("%s\n","ARP cache hit, forwarding ICMP error packet");
			memcpy(etherPacket->ether_dhost, (uint8_t *) arpEntry->mac, sizeof(uint8_t) * ETHER_ADDR_LEN);
			print_hdrs((uint8_t *)out, packetSize); 
			sr_send_packet(sr, out, packetSize, outInterface->name);
			free(out);
			free(arpEntry);
		} else {
			printf("%s\n","cant find MAC in ARP cache, sending ARP request");
			print_hdrs((uint8_t *)out, packetSize); 
			struct sr_arpreq *arpReq = sr_arpcache_queuereq(&(sr->cache), nextIP, out, packetSize, outInterface->name);//cant free out
			handle_arpreq(sr, arpReq);
		}
	}
	
}
/*---------------------------------------------------------------------
 * Method: sr_handlepacket(uint8_t* p,char* interface)
 * Scope:  Global
 *
 * This method is called each time the router receives a packet on the
 * interface.  The packet buffer, the packet length and the receiving
 * interface are passed in as parameters. The packet is complete with
 * ethernet headers.
 *
 * Note: Both the packet buffer and the character's memory are handled
 * by sr_vns_comm.c that means do NOT delete either.  Make a copy of the
 * packet instead if you intend to keep it around beyond the scope of
 * the method call.
 *
 *---------------------------------------------------------------------*/

void sr_handlepacket(struct sr_instance* sr,
        uint8_t * packet/* lent */,
        unsigned int len,
        char* interface/* lent */)
{
	/* REQUIRES */
	assert(sr);
	assert(packet);
	assert(interface);

	printf("*** -> Received packet of length %d \n",len);

	/* fill in code here */
	int minlength = sizeof(sr_ethernet_hdr_t);
	if (len < minlength) {
	fprintf(stderr, "Received ETHERNET header of insufficient length\n");
		return;
	}
	sr_ethernet_hdr_t *etherPacket = (sr_ethernet_hdr_t *)packet;
	uint16_t ethtype = ethertype(packet);
	//print_hdr_eth(buf);

	if (ethtype == ethertype_ip) { /* IP */
		minlength += sizeof(sr_ip_hdr_t);
		if (len < minlength) {
			fprintf(stderr, "Received IP header of insufficient length\n");
			return;
		}
		printf("%s\n", "Received IP header :");
		print_hdr_ip(packet + sizeof(sr_ethernet_hdr_t));
		uint8_t ip_proto = ip_protocol(packet + sizeof(sr_ethernet_hdr_t));
		struct sr_ip_hdr *ipPacket = (struct sr_ip_hdr*) (packet + sizeof(sr_ethernet_hdr_t));
		// verify checksum
		uint16_t sum = ipPacket->ip_sum;
		ipPacket->ip_sum = 0;
		uint16_t newSum = cksum(ipPacket, sizeof(sr_ip_hdr_t));
		if(sum != newSum) fprintf(stderr, "checksum unverified, package broken\n");

		uint32_t targetIP = ipPacket->ip_dst;
		uint32_t sourceIP = ipPacket->ip_src;
		struct sr_if *queryInterface = get_interface_from_ip(sr, targetIP);
		if(queryInterface != NULL){ // destined to router interfaces
			if (ip_proto == ip_protocol_icmp) { /* ICMP */
				minlength += sizeof(sr_icmp_hdr_t);
				if (len < minlength)
					fprintf(stderr, "Received ICMP header of insufficient length\n");

				printf("%s\n", "Received ICMP header :");
				print_hdr_icmp(packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
				sr_icmp_hdr_t * icmpPacket = (sr_icmp_hdr_t *) (packet + sizeof(sr_ethernet_hdr_t) + sizeof(sr_ip_hdr_t));
				if(icmpPacket->icmp_type == 8){ //icmp request
					printf("%s\n", "Received ICMP request, sending ICMP reply");
					send_icmp_reply(sr, packet, len, interface);
					printf("%s\n", "ICMP reply sent");
				}
				//else drop it
			}
			else{ //not icmp
				printf("%s\n", "NOT ICMP, sending port unreachable");
				send_icmp_error(sr, 3, 3, (uint8_t *)ipPacket, sourceIP, targetIP, ipPacket->ip_hl, (uint8_t *)etherPacket, sizeof(sr_ethernet_hdr_t));
				return;
			}
		}
		else{ // not destined to router interfaces, must forward
			printf("%s\n", "not destined to router interfaces, forwarding");
			ipPacket->ip_ttl--;
			if (ipPacket->ip_ttl <= 0) {
				printf("%s\n", "TTL expires, sending ICMP error reply");
				send_icmp_error(sr, 11, 0, (uint8_t *)ipPacket, sourceIP, targetIP, ipPacket->ip_hl, (uint8_t *)etherPacket, sizeof(sr_ethernet_hdr_t));
				return;
			} 
			else {
				ipPacket->ip_sum = 0;
				ipPacket->ip_sum = cksum(ipPacket, sizeof(sr_ip_hdr_t));
				struct sr_rt *entry = longestPrefix(sr->routing_table, targetIP);
				if(entry == NULL){
					send_icmp_error(sr, 3, 0, (uint8_t *)ipPacket, sourceIP, targetIP, ipPacket->ip_hl, (uint8_t *)etherPacket, sizeof(sr_ethernet_hdr_t));
					return;
				}
				uint32_t nextIP = (uint32_t) entry->dest.s_addr;
				struct sr_arpentry *arpEntry = sr_arpcache_lookup(&sr->cache, nextIP);

				if (arpEntry) {
					printf("%s\n","ARP cache hit, forwarding packet");
					struct sr_if *outInterface = sr_get_interface(sr, (char *) (entry->interface));

					memcpy(etherPacket->ether_dhost, arpEntry->mac, sizeof(uint8_t) * ETHER_ADDR_LEN);
					memcpy(etherPacket->ether_shost, (uint8_t *) outInterface->addr, sizeof(uint8_t) * ETHER_ADDR_LEN);
					uint8_t * copyPacket = malloc(sizeof(uint8_t) * len);
					memcpy(copyPacket, etherPacket, sizeof(uint8_t) * len);
					print_hdrs((uint8_t *)copyPacket, len); 
					sr_send_packet(sr, (uint8_t *)copyPacket, len, outInterface->name);
					free(copyPacket); //free copy
					free(arpEntry);
				} 
				else {
					printf("%s\n","cant find MAC in ARP cache, sending ARP request for ping");
					struct sr_arpreq *arpReq = sr_arpcache_queuereq(&(sr->cache), nextIP, packet, len, entry->interface);
					handle_arpreq(sr, arpReq);
				}
			}
		}
	}
	else if (ethtype == ethertype_arp) { /* ARP */
		minlength += sizeof(sr_arp_hdr_t);
		if (len < minlength)
			fprintf(stderr, "Received ARP header of insufficient length\n");
		else{
			sr_arp_hdr_t *arpPacket = (sr_arp_hdr_t *) (packet + sizeof(sr_ethernet_hdr_t));
			printf("%s\n", "Received ARP header :");
			print_hdr_arp((uint8_t *)arpPacket);

			unsigned short op = ntohs(arpPacket->ar_op);
			uint32_t targetIP = arpPacket->ar_tip;
			uint32_t sourceIP = arpPacket->ar_sip;
			struct sr_if *queryInterface = get_interface_from_ip(sr, targetIP);

			if(op == arp_op_request){ /* receive request */
				if(queryInterface != NULL){

					uint8_t * requestMAC = (uint8_t *) arpPacket->ar_sha;
					struct sr_arpreq * arpRequest = sr_arpcache_insert(&(sr->cache), requestMAC, sourceIP); //points to the req queue for this MAC
					printf("%s\n", "Received ARP request for MAC of router interfaces, giving ARP reply"); //!!!!!
					//printf("%s   %s\n", queryInterface->name, interface);
					uint8_t * copy = (uint8_t *)malloc(sizeof(uint8_t) * (sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t)));

					sr_ethernet_hdr_t * copyEtherPacket = (sr_ethernet_hdr_t *)copy;
					sr_arp_hdr_t *copyArpPacket = (sr_arp_hdr_t *) (copy + sizeof(sr_ethernet_hdr_t));
					uint8_t * sourceMAC = (uint8_t *) queryInterface->addr;
					uint8_t * destMAC = (uint8_t *) etherPacket->ether_shost;
					memcpy(copyEtherPacket->ether_shost, sourceMAC, sizeof(uint8_t) * ETHER_ADDR_LEN);
					memcpy(copyEtherPacket->ether_dhost, destMAC, sizeof(uint8_t) * ETHER_ADDR_LEN);
					copyEtherPacket->ether_type = htons(ethertype_arp);

					copyArpPacket->ar_hrd = htons(arp_hrd_ethernet);
					copyArpPacket->ar_pro = htons(ethertype_ip);
					copyArpPacket->ar_hln = 6;
					copyArpPacket->ar_pln = 4;
					copyArpPacket->ar_op = htons(arp_op_reply);
					memcpy(copyArpPacket->ar_sha, queryInterface->addr, sizeof(uint8_t) * ETHER_ADDR_LEN);
					memcpy(copyArpPacket->ar_tha, destMAC, sizeof(uint8_t) * ETHER_ADDR_LEN);
					copyArpPacket->ar_sip = queryInterface->ip;
					copyArpPacket->ar_tip = sourceIP;
					print_hdrs(copy, sizeof(sr_ethernet_hdr_t) + sizeof(sr_arp_hdr_t)); 
					sr_send_packet(sr, copy, len, queryInterface->name);// !!!interface or queryInterface
					free(copy);
				}
			}
			else if (op == arp_op_reply){ /* receive reply */
				if(queryInterface != NULL){
					printf("%s\n", "Received ARP reply for MAC of router interfaces, cache table now is :");
					uint8_t * requestMAC = (uint8_t *) arpPacket->ar_sha;
					struct sr_arpreq * arpRequest = sr_arpcache_insert(&(sr->cache), requestMAC, sourceIP); //points to the req queue for this MAC
					sr_arpcache_dump(&(sr->cache)); 
					if(arpRequest != NULL){ // send packets in req queue
						printf("%s\n", "sending packets in req queue");
						struct sr_packet *temp = arpRequest->packets;
						int num = 0;
						while (temp != NULL) {
							printf("sending %d th in queue\n", num);
							num++;
							etherPacket = (sr_ethernet_hdr_t *) temp->buf;

							uint8_t * destMAC = (uint8_t *) requestMAC; //request sender's MAC
							uint8_t * sourceMAC = (uint8_t *) queryInterface->addr; //interface which receives arp reply will send package
							memcpy(etherPacket->ether_shost, sourceMAC, sizeof(uint8_t) * ETHER_ADDR_LEN);
							memcpy(etherPacket->ether_dhost, destMAC, sizeof(uint8_t) * ETHER_ADDR_LEN); 
							uint8_t * copyPacket = malloc(sizeof(uint8_t) * temp->len);
     						memcpy(copyPacket, etherPacket, sizeof(uint8_t) * temp->len);
							print_hdrs((uint8_t *)copyPacket, temp->len); 
							sr_send_packet(sr, (uint8_t *)copyPacket, temp->len, temp->iface);
							free(copyPacket); //free copy
							temp = temp->next;
						}
						sr_arpreq_destroy(&(sr->cache), arpRequest); //free queue
					}
				}
			}
		}
	}
	else {
		fprintf(stderr, "Unrecognized Ethernet Type: %d\n", ethtype);
	}
} /* end sr_handlepacket */


/* Add any additional helper methods here & don't forget to also declare
them in sr_router.h.

If you use any of these methods in sr_arpcache.c, you must also forward declare
them in sr_arpcache.h to avoid circular dependencies. Since sr_router
already imports sr_arpcache.h, sr_arpcache cannot import sr_router.h -KM */
