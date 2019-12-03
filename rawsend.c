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
#include <netinet/in.h>
#include <netinet/ip.h>
#include <sys/uio.h>

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
{
  int length;
  int sockerr;
  socklen_t sockerr_size = sizeof sockerr;
  struct sockaddr_in dest_a;
  struct ip ih;
  struct udphdr uh;

#ifdef HAVE_SYS_UIO_H
  struct msghdr mh;
  struct iovec iov[3];
#else /* not HAVE_SYS_UIO_H */
  int flags = 0;
  static char *msgbuf = 0;
  static size_t msgbuflen = 0;
  static size_t next_alloc_size = 1;
#endif /* not HAVE_SYS_UIO_H */

  uh.uh_sport = saddr->sin_port;
  uh.uh_dport = daddr->sin_port;
  uh.uh_ulen = htons (msglen + sizeof uh);
  uh.uh_sum = flags & RAWSEND_COMPUTE_UDP_CHECKSUM
    ? udp_sum_calc (msglen,
		    ntohl(saddr->sin_addr.s_addr),
		    ntohs(saddr->sin_port),
		    ntohl(daddr->sin_addr.s_addr),
		    ntohs(daddr->sin_port),
		    msg)
    : 0;

  length = msglen + sizeof uh + sizeof ih;
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
  ih.ip_hl = (sizeof ih+3)/4;
  ih.ip_v = 4;
  ih.ip_tos = 0;
  /* Depending on the target platform, te ip_off and ip_len fields
     should be in either host or network byte order.  Usually
     BSD-derivatives require host byte order, but at least OpenBSD
     since version 2.1 and FreeBSD since 11.0 use network byte
     order.  Linux uses network byte order for all IP header fields. */
#if defined (__linux__) || (defined (__OpenBSD__) && (OpenBSD > 199702)) || (defined (__FreeBSD_version) && (__FreeBSD_version > 1100030))
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

  dest_a.sin_family = AF_INET;
  dest_a.sin_port = daddr->sin_port;
  dest_a.sin_addr.s_addr = daddr->sin_addr.s_addr;

#ifdef HAVE_SYS_UIO_H
  iov[0].iov_base = (char *) &ih;
  iov[0].iov_len = sizeof ih;
  iov[1].iov_base = (char *) &uh;
  iov[1].iov_len = sizeof uh;
  iov[2].iov_base = (char *) msg;
  iov[2].iov_len = msglen;

  bzero ((char *) &mh, sizeof mh);
  mh.msg_name = (char *)&dest_a;
  mh.msg_namelen = sizeof dest_a;
  mh.msg_iov = iov;
  mh.msg_iovlen = 3;

  if (sendmsg (s, &mh, 0) == -1)
#else /* not HAVE_SYS_UIO_H */
  memcpy (msgbuf+sizeof ih+sizeof uh, msg, msglen);
  memcpy (msgbuf+sizeof ih, & uh, sizeof uh);
  memcpy (msgbuf, & ih, sizeof ih);

  if (sendto (s, msgbuf, length, flags,
	      (struct sockaddr *)&dest_a, sizeof dest_a) == -1)
#endif /* not HAVE_SYS_UIO_H */
    {
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

extern int
make_raw_udp_socket (sockbuflen, af)
     size_t sockbuflen;
     int af;
{
  int s;
  if (af == AF_INET6)
    {
      fprintf (stderr, "Spoofing not supported for IPv6\n");
      return -1;
    }
  if ((s = socket (PF_INET, SOCK_RAW, IPPROTO_RAW)) == -1)
    return s;
  if (sockbuflen != -1)
    {
      if (setsockopt (s, SOL_SOCKET, SO_SNDBUF,
		      (char *) &sockbuflen, sizeof sockbuflen) == -1)
	{
	  fprintf (stderr, "setsockopt(SO_SNDBUF,%ld): %s\n",
		   sockbuflen, strerror (errno));
	}
      int so_broadcast = 1;
      if(setsockopt(s,SOL_SOCKET,SO_BROADCAST,&so_broadcast,sizeof so_broadcast) < 0)
      {
        fprintf (stderr, "setsockopt(SO_BROADCAST,%ld): %s\n",
                         sockbuflen, strerror (errno));
      }

    }

#ifdef IP_HDRINCL
  /* Some BSD-derived systems require the IP_HDRINCL socket option for
     header spoofing.  Contributed by Vladimir A. Jakovenko
     <vovik@lucky.net> */
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
