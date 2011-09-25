/*
 rawtest.c

 Date Created: Wed Jan 19 18:52:54 2000
 Author:       Simon Leinen  <simon.leinen@switch.ch>
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "rawsend.h"

#define MAX_IP_DATAGRAM_SIZE 65510

int
main (int argc, char **argv)
{
  int s;

  if ((s = make_raw_udp_socket (0, AF_INET)) == -1)
    {
      fprintf (stderr, "socket: %s\n",
	       strerror (errno));
      exit (1);
    }
  {
    char msg[MAX_IP_DATAGRAM_SIZE];
    int msglen = 1000;
    struct sockaddr_in here;
    struct sockaddr_in dest;
    memset ((char *) &here, 0, sizeof here);
    memset ((char *) &dest, 0, sizeof here);
    here.sin_addr.s_addr = htonl (0x823b0402);
    here.sin_port = htons (1234);
    dest.sin_addr.s_addr = htonl (0x7f000001);
    dest.sin_port = htons (5678);

    {
      int ttl = 64;
      int checksum_p = RAWSEND_COMPUTE_UDP_CHECKSUM;

      if (raw_send_from_to (s, & msg, msglen,
			    (struct sockaddr *) &here,
			    (struct sockaddr *) &dest,
			    ttl, checksum_p) == -1)
	{
	  fprintf (stderr, "sending failed: %s\n",
		   strerror (errno));
	  exit (1);
	}
    }
  }
  return 0;
}
