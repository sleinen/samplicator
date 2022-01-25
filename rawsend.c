/*
 rawsend.c

 Date Created: Tue Jan 18 12:13:31 2000
 Author:       Simon Leinen  <simon.leinen@switch.ch>

 Send a UDP datagram to a given destination address, but make it look
 as if it came from a given transport address (IP address and port
 number).
 */

#include "config.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <sys/types.h>
#include <inttypes.h>
#include <sys/param.h>
#include <string.h>
#if STDC_HEADERS
# define bzero(b,n) memset(b,0,n)
#else
# include <strings.h>
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
# endif
#endif
#ifdef HAVE_NETINET_IN_SYSTM_H
#include <netinet/in_systm.h>
#endif
#include <sys/socket.h>

#ifndef __APPLE_USE_RFC_3542
#define __APPLE_USE_RFC_3542
#endif

#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <sys/uio.h>
#include <sys/ioctl.h>

/* make uh_... slot names available under Linux */
#define __FAVOR_BSD 1

#include <netinet/udp.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "rawsend.h"

#define MAX_IP_DATAGRAM_SIZE 65535

static unsigned ip_header_checksum (const void * header);
static uint16_t udp_sum_calc (uint16_t, uint32_t, uint16_t, uint32_t, uint16_t, const void *);
static uint16_t udp_sum_calc_v6 (struct in6_addr, uint16_t, struct in6_addr, uint16_t, uint16_t, const char *);

struct in6_pktinfo {
        struct in6_addr ipi6_addr;
        int             ipi6_ifindex;
};

int
raw_send_from_to (s, msg, msglen, saddr_generic, daddr_generic, ttl, flags)
     int s;
     const void * msg;
     size_t msglen;
     struct sockaddr *saddr_generic;
     struct sockaddr *daddr_generic;
     int ttl;
     int flags;
#define saddr ((struct sockaddr_in *) saddr_generic)
#define daddr ((struct sockaddr_in *) daddr_generic)
#define saddr_v6 ((struct sockaddr_in6 *) saddr_generic)
#define daddr_v6 ((struct sockaddr_in6 *) daddr_generic)
#define ipv6 (saddr_generic->sa_family == AF_INET6 && daddr_generic->sa_family == AF_INET6)
{
  int length;
  int sockerr;
  socklen_t sockerr_size = sizeof sockerr;
  struct sockaddr_storage dest_a;
  struct ip ih;
  struct udphdr uh;

#ifdef HAVE_SYS_UIO_H
  struct msghdr mh = {};
  struct iovec iov[3];
#else /* not HAVE_SYS_UIO_H */
  flags = 0;
  static char *msgbuf = 0;
  static size_t msgbuflen = 0;
  static size_t next_alloc_size = 1;
#endif /* not HAVE_SYS_UIO_H */

  if( ipv6 ) {
    length = msglen;
  } else {
	  uh.uh_sport = saddr->sin_port;
	  uh.uh_dport = daddr->sin_port;
	  uh.uh_sum = flags & RAWSEND_COMPUTE_UDP_CHECKSUM 
		  ? udp_sum_calc (msglen,
						  ntohl(saddr->sin_addr.s_addr),
						  ntohs(saddr->sin_port),
						  ntohl(daddr->sin_addr.s_addr),
						  ntohs(daddr->sin_port),
						  msg)
		  : 0;
	  length = msglen + sizeof uh + sizeof ih;
  }  
  uh.uh_ulen = htons (msglen + sizeof uh);
  
#ifndef HAVE_SYS_UIO_H
  if (length > msgbuflen)
    {
      if (length > MAX_IP_DATAGRAM_SIZE)
	{
	  return -1;
	}
      if (msgbuf != (char *) 0)
	free (msgbuf);
      while (next_alloc_size < length)
	next_alloc_size *= 2;
      if ((msgbuf = malloc (next_alloc_size)) == (char *) 0)
	{
	  fprintf (stderr, "Out of memory!\n");
	  return -1;
	}
      msgbuflen = next_alloc_size;
      next_alloc_size *= 2;
    }
#endif /* not HAVE_SYS_UIO_H */

  if( ipv6 ) {
	  ((struct sockaddr_in6*) &dest_a)->sin6_family = AF_INET6;
	  ((struct sockaddr_in6*) &dest_a)->sin6_port = daddr_v6->sin6_port;
	  ((struct sockaddr_in6*) &dest_a)->sin6_addr = daddr_v6->sin6_addr;	  
  } else {
	  ih.ip_hl = (sizeof ih+3)/4;
	  ih.ip_v = 4;
	  ih.ip_tos = 0;

	  /* Depending on the target platform, te ip_off and ip_len fields
		 should be in either host or network byte order.  Usually
		 BSD-derivatives require host byte order, but at least OpenBSD
		 since version 2.1 uses network byte order.  Linux uses network
		 byte order for all IP header fields. */
#if defined (__linux__) || (defined (__OpenBSD__) && (OpenBSD > 199702))
	  ih.ip_len = htons (length);
	  ih.ip_off = htons (0);
#else 
	  ih.ip_len = length;
	  ih.ip_off = 0;
#endif
	  ih.ip_id = htons (0);
	  ih.ip_ttl = ttl;
	  ih.ip_p = 17;
	  ih.ip_sum = htons (0);
	  ih.ip_src.s_addr = saddr->sin_addr.s_addr;
	  ih.ip_dst.s_addr = daddr->sin_addr.s_addr;

	  /* At least on Solaris, it seems clear that even the raw IP datagram
		 transmission code will actually compute the IP header checksum
		 for us.  Probably this is the case for all other systems on which
		 this code works, so maybe we should just set the checksum to zero
		 to avoid duplicate work.  I'm not even sure whether my IP
		 checksum computation in ip_header_checksum() below is correct. */
	  ih.ip_sum = ip_header_checksum (&ih);
  
	  ((struct sockaddr_in*) &dest_a)->sin_family = AF_INET;
	  ((struct sockaddr_in*) &dest_a)->sin_port = daddr->sin_port;
	  ((struct sockaddr_in*) &dest_a)->sin_addr.s_addr = daddr->sin_addr.s_addr;
  }
  int err = 0;
#ifdef HAVE_SYS_UIO_H

  if( ipv6 ) {
	  iov[0].iov_base = (char *) msg;
	  iov[0].iov_len = msglen;
	  mh.msg_iovlen = 1;
	  
	  // Set the PKT information to spoof the source
	  size_t cmsglen = CMSG_SPACE (sizeof (struct in6_pktinfo));
	  mh.msg_control = calloc (1,cmsglen);	  
	  mh.msg_controllen = cmsglen;

	  struct cmsghdr *hdr1 = CMSG_FIRSTHDR(&mh);
	  hdr1->cmsg_level = IPPROTO_IPV6;
	  hdr1->cmsg_type = IPV6_PKTINFO;
	  hdr1->cmsg_len = CMSG_LEN (sizeof (struct in6_pktinfo));
	  struct in6_pktinfo *pktinfo = (struct in6_pktinfo *) CMSG_DATA (hdr1);
	  pktinfo->ipi6_ifindex = 0;
	  pktinfo->ipi6_addr = saddr_v6->sin6_addr;
  } else {
	  iov[0].iov_base = (char *) &ih;
	  iov[0].iov_len = sizeof ih;
	  iov[1].iov_base = (char *) &uh;
	  iov[1].iov_len = sizeof uh;
	  iov[2].iov_base = (char *) msg;
	  iov[2].iov_len = msglen;
	  mh.msg_iovlen = 3;
  }


  mh.msg_name = (char *)&dest_a;
  mh.msg_namelen = sizeof dest_a;
  mh.msg_iov = iov;

  if( sendmsg (s, &mh, 0) == -1 ) {
#else /* not HAVE_SYS_UIO_H */
  if( ipv6 ) {
	  memcpy (msgbuf, msg, msglen);
  } else { 
	  memcpy (msgbuf+sizeof ih+sizeof uh, msg, msglen);
	  memcpy (msgbuf+sizeof ih, & uh, sizeof uh);
	  memcpy (msgbuf, & ih, sizeof ih);
  }

  if( sendto (s, msgbuf, length, flags,
			  (struct sockaddr *)&dest_a, sizeof dest_a) == -1 ) {
#endif /* not HAVE_SYS_UIO_H */
	  if (getsockopt (s, SOL_SOCKET, SO_ERROR, (char *) &sockerr, &sockerr_size) == 0)
	    {
			fprintf (stderr, "socket error: %d\n", sockerr);
			fprintf (stderr, "socket: %s\n",
					 strerror (errno));
		}
        return -1;
    }
  return 0;
}
#undef saddr
#undef daddr
#undef saddr_v6
#undef daddr_v6

extern int
make_raw_udp_socket (sockbuflen, af)
     size_t sockbuflen;
     int af;
{
  int s;
  int i=0;	  
  if ((s = socket ( af == AF_INET ? PF_INET : PF_INET6 , af == AF_INET ? SOCK_RAW : SOCK_DGRAM, af == AF_INET ? IPPROTO_RAW : IPPROTO_UDP)) == -1)
	  return s;
  if (sockbuflen != -1)
	  {
		  if (setsockopt (s, SOL_SOCKET, SO_SNDBUF,
						  (char *) &sockbuflen, sizeof sockbuflen) == -1)
			  {
				  fprintf (stderr, "setsockopt(SO_SNDBUF,%ld): %s\n",
						   sockbuflen, strerror (errno));
			  }
	  }
#ifdef IP_HDRINCL
	  /* Some BSD-derived systems require the IP_HDRINCL socket option for
		 header spoofing.  Contributed by Vladimir A. Jakovenko
		 <vovik@lucky.net> */
  if (af == AF_INET )
	  {
		  int on = 1;
		  if (setsockopt (s, IPPROTO_IP, IP_HDRINCL, (char *) &on, sizeof(on)) < 0)
			  {
				  fprintf (stderr, "setsockopt(IP_HDRINCL,%d): %s\n",
						   on, strerror (errno));
			  }
	  }
#endif /* IP_HDRINCL */  
  return s;
}

/* unsigned ip_header_checksum (header)

   Compute IP header checksum IN NETWORK BYTE ORDER.

   This is defined in RFC 760 as "the 16 bit one's complement of the
   one's complement sum of all 16 bit words in the header.  For
   purposes of computing the checksum, the value of the checksum field
   is zero.".
*/
static unsigned
ip_header_checksum (const void * header)
{
  unsigned long csum = 0;
  unsigned size = ((struct ip *) header)->ip_hl;
  uint16_t *h = (uint16_t *) header;
  unsigned k;

  /* Interestingly, we don't need to convert between network and host
     byte order because of the way the checksum is defined. */
  for (k = 0; k < size; ++k)
    {
      csum += *h++, csum += *h++;
    }
  while (csum > 0xffff)
    {
      csum = (csum & 0xffff) + (csum >> 16);
    }
  return ~csum & 0xffff;
}

/* IPv6 UDP header as per RFC 2460 */
struct udp_v6_pseudo_header {
	struct in6_addr src_addr;
	struct in6_addr dest_addr;
	uint32_t len_udp;
	uint32_t zeros:24 ;
	uint8_t next_header;
	uint16_t src_port;
	uint16_t dest_port;
	uint16_t len;
} __attribute__((packed));

/* This union will allow us to calculate the header with a simple for loop */
union udp_v6_sum {
	struct udp_v6_pseudo_header p_hdr;
	uint16_t hdr[23];
};
	
uint16_t udp_sum_calc_v6( 
						  struct in6_addr src_addr,
						  uint16_t src_port,
						  struct in6_addr dest_addr,
  						  uint16_t dest_port,
						  uint16_t len_udp,
						  const char *buff
						  )
{
	union udp_v6_sum sum;
	uint16_t pad = 0;
	uint32_t csum = 0;
	size_t i = 0;
		
	bzero ( &sum , sizeof (union udp_v6_sum));
	sum.p_hdr.src_addr=src_addr;
	sum.p_hdr.dest_addr=dest_addr;
	sum.p_hdr.len_udp = htons(len_udp+8);
	sum.p_hdr.next_header = 17;
	sum.p_hdr.src_port = src_port;
	sum.p_hdr.dest_port = dest_port;
	sum.p_hdr.len = htons(len_udp+8);
	
	for( i = 0; i < 24 ; i++ ) {
		csum += (uint32_t)ntohs(sum.hdr[i]);
	}
	
	if( len_udp%2 != 0 ) {
		pad = 1;
	}
	
	for( i=0; i< ( len_udp - pad ); i+=2 ) {
		csum += (uint32_t) ntohs(*(uint16_t*) buff);
		buff += 2;
	}

	if( pad ) {
		csum += (uint32_t) ( *(uint8_t *) buff );
	}
	
	while ( csum>>16 ) {
          csum = ( csum & 0xFFFF ) + ( csum >> 16 );
	}

    return htons((uint16_t) ~csum) ;
}

uint16_t udp_sum_calc( uint16_t len_udp,
		  uint32_t src_addr,
		  uint16_t src_port,
		  uint32_t dest_addr,
		  uint16_t dest_port,
		  const void * buff
		)
{
	uint16_t prot_udp        = 17;
	uint16_t chksum_init     = 0;
	uint16_t udp_len_total   = 0;
	uint32_t sum             = 0;
	uint16_t pad             = 0;
	uint16_t low;
	uint16_t high;
	int i;

	/* if we have an odd number of bytes in the data payload, then set the pad to 1
	 * for special processing
	 */
	if( len_udp%2 != 0 ) {
	  pad = 1;
	}
	/* do the source and destination addresses, first, we have to split them
	 * into 2 shorts instead of the 32 long as sent.  Sorry, that's just how they
	 * calculate
	 */
	low  = src_addr;
	high = ( src_addr>>16 );
	sum  += ( ( uint32_t ) high + ( uint32_t ) low );

	/* now do the same with the destination address */
	low  = dest_addr;
	high = ( dest_addr>>16 );
	sum  += ( ( uint32_t ) high + ( uint32_t ) low );

	/* the protocol and the number and the length of the UDP packet */
	udp_len_total = len_udp + 8;  /* length sent is length of data, need to add 8 */
	sum += ( ( uint32_t )prot_udp + ( uint32_t )udp_len_total );


	/* next comes the source and destination ports */
	sum += ( ( uint32_t )src_port + ( uint32_t ) dest_port );

	/* Now add the UDP length and checksum=0 bits 
	 * The Length will always be 8 bytes plus the length of the udp data sent
	 * and the checksum will always be zero
	 */
	sum += ( ( uint32_t ) udp_len_total + ( uint32_t ) chksum_init );
        

	/* Add all 16 bit words to the sum, if pad is set (ie, odd data length) this will just read up
	 * to the last full 16 bit word.
	 * */
        for( i=0; i< ( len_udp - pad ); i+=2 ) {
          high  = ntohs(*(uint16_t *)buff);
	  buff +=2;
	  sum  += ( uint32_t ) high;
	}

	/* ok, if pad is true, then the pointer is now  right before the last single byte in 
	 * the payload.  We only need to add till the end of the string (1-byte) , not the next 2 bytes
	 * as above.
	 */
	if( pad ) {
	  sum += ntohs( * ( unsigned char * ) buff );
	}

	/* keep only the last 16 bits of the 32 bit calculated sum and add the carry overs */
	while ( sum>>16 ) {
          sum = ( sum & 0xFFFF ) + ( sum >> 16 );
	}

	/* one's compliment the sum */
        sum = ~sum;

	/* finally, return the 16bit network formated checksum */
        return ((uint16_t) htons(sum) );
};
