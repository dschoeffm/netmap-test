#include <net/netmap_user.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <ifaddrs.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

//! ethernet header
struct ethernet_header{
	uint8_t dst_mac[6]; //!< destination MAC
	uint8_t src_mac[6]; //!< source MAC
	uint16_t ethertype; //!< L2_SDU type
};

//! IPv4 header
struct ipv4_header{
	uint8_t ihl_version; //!< IP version
	uint8_t tos; //!< Type of Service
	uint16_t total_length; //!< length of packet in octetts
	uint16_t id; //!< ID for fragmentation
	uint16_t flags_frag; //!< flags and fragmentation offset
	uint8_t ttl; //!< Time to live
	uint8_t proto; //!< Protocol after IPv4 header
	uint16_t checksum; //!< Header checksum
	uint32_t src_ip; //!< source IP address
	uint32_t dst_ip; //!< destination IP address
	// ignore options
};

//! UDP header
struct udp_header{
	uint16_t src_port; //!< source port
	uint16_t dst_port; //!< destination port
	uint16_t length; //!< length including header
	uint16_t checksum; //!< header checksum
};

void hexdump(uint8_t* p, unsigned int bytes){
	printf("Dump of address: %p, %u bytes", p, bytes);
	uint8_t* end = p+bytes;
	int counter = 0;
	while(p < end){
		if((counter & 0xf) == 0 ){ printf("\n  %04x:  ", counter);}
		printf(" %02x%02x", *p, *(p+1));
		p += 2;
		counter += 2;
	}
	printf("\n");
}

/*
uint32_t fetch_tx_pkts(struct nm_device* dev){
	return __atomic_fetch_and(&dev->tx_pkts, 0x0, __ATOMIC_RELAXED);
}
uint32_t fetch_rx_pkts(struct nm_device* dev){
	return __atomic_fetch_and(&dev->rx_pkts, 0x0, __ATOMIC_RELAXED);
}
uint64_t fetch_tx_octetts(struct nm_device* dev){
	return __atomic_fetch_and(&dev->tx_octetts, 0x0, __ATOMIC_RELAXED);
}
uint64_t fetch_rx_octetts(struct nm_device* dev){
	return __atomic_fetch_and(&dev->rx_octetts, 0x0, __ATOMIC_RELAXED);
}
*/

int sent_packets = 0;

void* printer(void* v){
	if(v){};
	while(1){
		printf("sent_packets: %d\n", sent_packets / 1000000);
		sent_packets = 0;
		sleep(1);
	}
	return NULL;
}


int main (int argc, char** argv){
	if(argc < 2){
		printf("FAIL\n");
		return 0;
	}
	char* port = argv[1];
	void* nm_mmap;

	if(port == NULL){
		printf("nm_config(): config is NULL\n");
		return 1;
	}

	struct nmreq* nmr = (struct nmreq*) malloc(sizeof(struct nmreq));

	strncpy(nmr->nr_name, port, 16);
	nmr->nr_version = NETMAP_API;
	nmr->nr_flags = NR_REG_ONE_NIC;
	nmr->nr_tx_rings = 1;
	nmr->nr_rx_rings = 1;
	nmr->nr_ringid = 0;

	int fd = open("/dev/netmap", O_RDWR);
	if(fd == -1){
		printf("nm_config(): could not open device /dev/netmap\n");
		return 1;
	}

	int ret = ioctl(fd, NIOCREGIF, nmr);
	if(ret == -1){
		printf("nm_config(): error issuing NIOCREFIF\n");
		return 1;
	}

	nm_mmap = mmap(NULL , nmr->nr_memsize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	struct netmap_if* nifp = NETMAP_IF(nm_mmap, nmr->nr_offset);

	struct netmap_ring* ring = NETMAP_TXRING(nifp, 0);

	for(unsigned int i=0; i<ring->num_slots; i++){
		char* buf = NETMAP_BUF(ring, ring->slot[i].buf_idx);
		struct ethernet_header* buf_ether = (struct ethernet_header*) buf;
		struct ipv4_header* buf_ipv4 = (struct ipv4_header*) (buf + sizeof(struct ethernet_header));
		struct udp_header* buf_udp = (struct udp_header*)
			(buf + sizeof(struct ethernet_header) + sizeof(struct ipv4_header));
		for(int i=0; i<6; i++){
			buf_ether->dst_mac[i] = 0xff;
			buf_ether->ethertype = htons(0x0800);
		}
		buf_ipv4->ihl_version = 0x45;
		buf_ipv4->src_ip = htonl(0x01020304);
		buf_ipv4->dst_ip = htonl(0x11121314);
		buf_udp->dst_port = htons(1025);
		buf_udp->src_port = htons(1026);
		ring->slot[i].len = 60;
		if(i % 128 == 0){
			ring->slot[i].flags |= NS_REPORT;
		}
	}

	pthread_t thread;
	pthread_create(&thread, NULL, printer, NULL);

	while(1){
		/*
		if(ring->head < ring->tail){
			sent_packets += ring->tail - ring->head;
		}
		else {
			sent_packets += ring->num_slots - ring->head + ring->tail;
		}
		*/
		ring->head = ring->tail;
		ring->cur = ring->tail;
		ioctl(fd, NIOCTXSYNC);
	}

	pthread_join(thread, NULL);
}
