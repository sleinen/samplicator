/*
 rawsend.c

 Date Created: Tue Jan 18 12:13:31 2000
 Author:       Simon Leinen  <simon@limmat.switch.ch>

 Send a UDP datagram to a given destination address, but make it look
 as if it came from a given transport address (IP address and port
 number).
 */

#include "config.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
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

#define DEFAULT_TTL 64

static unsigned ip_header_checksum (const void * header);

int
raw_send_from_to (s, msg, msglen, saddr, daddr)
     int s;
     const void * msg;
     size_t msglen;
     struct sockaddr_in *saddr;
     struct sockaddr_in *daddr;
{
  int length;
  int flags = 0;
  int sockerr;
  int sockerr_size = sizeof sockerr;
  struct sockaddr_in dest_a;
  struct ip ih;
  struct udphdr uh;

#ifdef HAVE_SYS_UIO_H
  struct msghdr mh;
  struct iovec iov[3];
#else /* not HAVE_SYS_UIO_H */
  static char *msgbuf = 0;
  static size_t msgbuflen = 0;
  static size_t next_alloc_size = 1;
#endif /* not HAVE_SYS_UIO_H */

  uh.uh_sport = saddr->sin_port;
  uh.uh_dport = daddr->sin_port;
  uh.uh_ulen = htons (msglen + sizeof uh);
  /* It would be nice if we'd actually compute the UDP checksum,
     because that's the only protection against transmission errors of
     the copied packets. */
  uh.uh_sum = 0;

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
  ih.ip_len = length;
  ih.ip_id = htons (0);
  ih.ip_off = htons (0);
  ih.ip_ttl = DEFAULT_TTL;
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
  dest_a.sin_port = IPPROTO_UDP;
  dest_a.sin_addr.s_addr = htonl (0x7f000001);

#ifdef HAVE_SYS_UIO_H
  iov[0].iov_base = (char *) &ih;
  iov[0].iov_len = sizeof ih;
  iov[1].iov_base = (char *) &uh;
  iov[1].iov_len = sizeof uh;
  iov[2].iov_base = (char *) msg;
  iov[2].iov_len = msglen;

  bzero ((char *) &mh, sizeof mh);
  mh.msg_name = (struct sockaddr *)&dest_a;
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
make_raw_udp_socket (sockbuflen)
     long sockbuflen;
{
  int s;
  if ((s = socket (PF_INET, SOCK_RAW, IPPROTO_RAW)) == -1)
    return s;
  if (sockbuflen != -1)
    {
      if (setsockopt (s, SOL_SOCKET, SO_SNDBUF, (char *) &sockbuflen, sizeof sockbuflen) == -1)
	{
	  fprintf (stderr, "setsockopt(SO_SNDBUF,%ld): %s\n",
		   sockbuflen, strerror (errno));
	}
    }
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
      csum += *h++ + *h++;
    }
  while (csum > 0xffff)
    {
      csum = (csum & 0xffff) + (csum >> 16);
    }
  return ~csum & 0xffff;
}
