/*
 rawsend.c

 Date Created: Tue Jan 18 12:13:31 2000
 Author:       Simon Leinen  <simon@limmat.switch.ch>

 Send a UDP datagram to a given destination address, but make it look
 as if it came from a given source address.
 */

#include "config.h"

#include <sys/types.h>
#include <string.h>
#if STDC_HEADERS
# define bzero(b,n) memset(b,0,n)
#else
# include <strings.h>
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
# endif
#endif
#ifdef __sun__
#define USE_BSD 1
#define __FAVOR_BSD 1
#include <netinet/in_systm.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "rawsend.h"

#define DEFAULT_TTL 64

static unsigned ip_header_checksum (const void * header);

static unsigned
ip_header_checksum (const void * header)
{
  unsigned long csum = 0;
#ifdef USE_BSD
  unsigned size = ((struct ip *) header)->ip_hl;
#else /* not USE_BSD */
  unsigned size = ((struct iphdr *) header)->ihl;
#endif /* not USE_BSD */
  uint16_t *h = (uint16_t *) header;
  unsigned k;
  for (k = 0; k < size; ++k)
    {
      csum ^= h[2*k];
      csum ^= h[2*k+1];
    }
  return ~csum;
}

int
raw_send_from_to (s, msg, msglen, saddr, daddr)
     int s;
     const void * msg;
     size_t msglen;
     struct sockaddr_in *saddr;
     struct sockaddr_in *daddr;
{
  char message[MAX_IP_DATAGRAM_SIZE];
  int length;
  int flags = 0;
  int sockerr;
  int sockerr_size = sizeof sockerr;
  struct sockaddr_in dest_a;
#ifdef USE_BSD
  struct ip ih;
#else /* not USE_BSD */
  struct iphdr ih;
#endif /* not USE_BSD */
  struct udphdr uh;

#ifdef __FAVOR_BSD
  uh.uh_sport = saddr->sin_port;
  uh.uh_dport = daddr->sin_port;
  uh.uh_ulen = htons (msglen + sizeof uh);
  uh.uh_sum = 0;
#else
  uh.source = saddr->sin_port;
  uh.dest = daddr->sin_port;
  uh.len = htons (msglen + sizeof uh);
  uh.check = 0;
#endif

  length = msglen + sizeof uh + sizeof ih;
  if (length > MAX_IP_DATAGRAM_SIZE)
    {
      return -1;
    }

#ifdef USE_BSD
  ih.ip_hl = (sizeof ih+3)/4;
  ih.ip_v = 4;
  ih.ip_tos = 0;
  ih.ip_len = length;
  ih.ip_id = htons (0);
  ih.ip_off = htons (0);
  ih.ip_ttl = DEFAULT_TTL;
  ih.ip_p = 17;
  ih.ip_sum = htons (0);
  ih.ip_src.s_addr = saddr->sin_addr.s_addr;
  ih.ip_dst.s_addr = daddr->sin_addr.s_addr;
  ih.ip_sum = htons (ip_header_checksum (&ih));
#else /* not USE_BSD */
  ih.ihl = (sizeof ih+3)/4;
  ih.version = 4;
  ih.tos = 0;
  ih.tot_len = length;
  ih.id = htons (0);
  ih.frag_off = htons (0);
  ih.ttl = DEFAULT_TTL;
  ih.protocol = 17;
  ih.check = htons (0);
  ih.saddr = saddr->sin_addr.s_addr;
  ih.daddr = daddr->sin_addr.s_addr;
  ih.check = htons (ip_header_checksum (&ih));
#endif /* not USE_BSD */

  memcpy (message+sizeof ih+sizeof uh, msg, msglen);
  memcpy (message+sizeof ih, & uh, sizeof uh);
  memcpy (message, & ih, sizeof ih);

  dest_a.sin_family = AF_INET;
  dest_a.sin_port = IPPROTO_UDP;
  dest_a.sin_addr.s_addr = htonl (0x7f000001);

  if (sendto (s, message, length, flags,
	      (struct sockaddr *)&dest_a, sizeof dest_a) == -1)
    {
      if (getsockopt (s, SOL_SOCKET, SO_ERROR, &sockerr, &sockerr_size) == 0)
	{
	  fprintf (stderr, "socket error: %d\n", sockerr);
	  fprintf (stderr, "socket: %s\n",
		   strerror (errno));
	  exit (1);
	}
    }
}

extern int
make_raw_udp_socket (dest)
     struct sockaddr_in *dest;
{
  return socket (PF_INET, SOCK_RAW, IPPROTO_RAW);
}
