/*
 rawtest.c

 Date Created: Wed Jan 19 18:52:54 2000
 Author:       Simon Leinen  <simon@limmat.switch.ch>
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

#include "rawsend.h"

int
main (int argc, char **argv)
{
  int s;

  if ((s = socket (PF_INET, SOCK_RAW, IPPROTO_RAW)) == -1)
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

    if (raw_send_from_to (s, & msg, msglen,
			  & here, sizeof here,
			  &dest, sizeof dest) == -1)
      {
	fprintf (stderr, "sending failed: %s\n",
		 strerror (errno));
	exit (1);
      }
  }
  return 0;
}
