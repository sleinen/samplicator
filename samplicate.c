#include "config.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#include <stdio.h>
#include <string.h>
#include <errno.h>
#if STDC_HEADERS
# define bzero(b,n) memset(b,0,n)
#else
# include <strings.h>
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
#  define memmove(d, s, n) bcopy ((s), (d), (n))
# endif
#endif

#include "rawsend.h"

#define PDU_SIZE 1500

#define FLOWPORT 2000 

static int debug;

enum peer_flags
{
  pf_RAW	= 0x0001,
};

struct peer {
  int			fd;
  struct sockaddr_in	addr;
  int			port;
  int			freq;
  int			freqcount;
  enum peer_flags	flags;
};

static void usage(const char *);
static int send_pdu_to_peer (struct peer *, const void *, size_t,
			     struct sockaddr_in *);

/* Work around a GCC compatibility problem with respect to the
   inet_ntoa() system function */
#undef inet_ntoa
#define inet_ntoa(x) my_inet_ntoa(&(x))

static const char *
my_inet_ntoa (const struct in_addr *in)
{
  unsigned a = ntohl (in->s_addr);
  static char buffer[16];
  sprintf (buffer, "%d.%d.%d.%d",
	   (a >> 24) & 0xff,
	   (a >> 16) & 0xff,
	   (a >> 8) & 0xff,
	   a & 0xff);
  return buffer;
}

int main(argc, argv)
int argc;
char **argv;
{
  extern char *optarg;
  extern int errno, optind;
  unsigned char fpdu[PDU_SIZE];
  struct sockaddr_in local_address;
  struct sockaddr_in remote_address;
  int fsockfd;
  int fport, i, len, n;
  int npeers, tx_delay;
  struct peer *peers;
  char tmp_buf[256];
  char *c;
  int raw_p = 0;

  fport = FLOWPORT;
  debug = 0;
  tx_delay = 0;

  while ((i = getopt (argc, argv, "hd:p:x:r")) != -1)
    switch (i) {
    case 'd': /* debug */
      debug = atoi (optarg);
      break;
    case 'p': /* flow Port */
      fport = atoi (optarg);
      break;
    case 'x': /* transmit delay */
      tx_delay = atoi (optarg);
      break;
    case 'r': /* raw */
      raw_p = 1;
      break;
    case 'h': /* help */
      usage (argv[0]);
      exit (0);
      break;
    default:
      usage (argv[0]);
      exit (1);
      break;
    }

  /* allocate argc - optind peer entries */
  npeers = argc - optind;

  if (!(peers = (struct peer*) malloc (npeers * sizeof (struct peer)))) {
    fprintf(stderr, "malloc(): failed.\n");
    exit (1);
  }

  /* zero out malloc'd memory */
  bzero(peers, npeers*sizeof (struct peer));

  /* fill in peer entries */
  for (i = optind, n = 0; i < argc; ++i, ++n)
    {
      if (raw_p)
	{
	  peers[n].flags |= pf_RAW;
	}
      if (strlen (argv[i]) > 255)
	{
	  fprintf (stderr, "ouch!\n");
	  exit (1);
	}
      strcpy (tmp_buf, argv[i]);

      /* skip to end or port seperator */
      for (c = tmp_buf; (*c != '/') && (*c); ++c);

      /* extract the port part */
      if (*c == '/')
	{
	  *c = 0;
	  ++c;
	  peers[n].addr.sin_port = htons (atoi(c));
	}
      else 
	peers[n].addr.sin_port = htons (FLOWPORT);

      /* extract the frequency part */
      for (; (*c != '/') && (*c); ++c)
	;
      if (*c == '/')
	{
	  *c = 0;
	  ++c;
	  peers[n].freq = atoi(c);
	}
      else
	peers[n].freq = 1;
      peers[n].freqcount = 0;

      /* printf("Frequency: %d\n", peers[n].freq); */

      /* extract the ip address part */
      if (inet_aton (tmp_buf, & peers[n].addr.sin_addr) == 0)
	{
	  fprintf (stderr, "parsing IP address failed\n");
	  exit (1);
	}

      peers[n].addr.sin_family = AF_INET;

      if (peers[n].flags & pf_RAW) {
	if ((peers[n].fd = make_raw_udp_socket (& peers[n].addr)) < 0)
	  {
	    if (errno == EPERM)
	      {
		fprintf (stderr, "Not enough privilege for -r option---try again as root.\n");
	      }
	    else
	      {
		fprintf (stderr, "creating raw socket: %s\n", strerror(errno));
	      }
	    exit (1);
	  }
      } else {
	if ((peers[n].fd = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
	  {
	    fprintf (stderr, "socket(): %s\n", strerror(errno));
	    exit (1);
	  }
      }
    }

  /* setup to receive flows */
  bzero (&local_address, sizeof local_address);
  local_address.sin_family = AF_INET;
  local_address.sin_addr.s_addr = htonl (INADDR_ANY);
  local_address.sin_port = htons (fport);

  if ((fsockfd = socket (AF_INET, SOCK_DGRAM, 0)) < 0) {
    fprintf (stderr, "socket(): %s\n", strerror (errno));
    exit(1);
  }
  if (bind (fsockfd, (struct sockaddr*)&local_address, sizeof local_address) < 0) {
    fprintf (stderr, "bind(): %s\n", strerror (errno));
    exit (1);
  }

  while (1)
    {
      len = sizeof remote_address;
      if ((n = recvfrom (fsockfd, (char*)fpdu,
			 sizeof (fpdu), 0,
			 (struct sockaddr*) &remote_address, &len)) == -1) {
	fprintf(stderr, "recvfrom(): %s\n", strerror(errno));
	exit (1);
      }
      if (len != sizeof remote_address)
	{
	  fprintf (stderr, "recvfrom() return address length %d - expected %d\n",
		   len, sizeof remote_address);
	  exit (1);
	}
      if (debug)
	{
	  fprintf (stderr, "received %d bytes from %s:%d\n",
		   n,
		   inet_ntoa (remote_address.sin_addr),
		   (int) ntohs (remote_address.sin_port));
	}
      for (i = 0; i < npeers; ++i)
	{
	  if (peers[i].freqcount == 0)
	    {
	      if (send_pdu_to_peer (& (peers[i]), fpdu, n, &remote_address)
		  == -1)
		{
		  fprintf (stderr, "sendto(%s:%d) failed: %s",
			   inet_ntoa (peers[i].addr.sin_addr),
			   (int) ntohs (peers[i].addr.sin_port),
			   strerror (errno));
		}
	      else if (debug)
		{
		  fprintf (stderr, "  sent to %s:%d\n",
			   inet_ntoa (peers[i].addr.sin_addr),
			   (int) ntohs (peers[i].addr.sin_port)); 
		}
	      peers[i].freqcount = peers[i].freq-1;
	    }
	  else
	    {
	      --peers[i].freqcount;
	    }
	  if (tx_delay)
	    usleep ((unsigned)tx_delay);
	}
    }
}

static void
usage (progname)
     const char *progname;
{
  fprintf (stderr, "Usage: %s [option...] receiver...\n\
\n\
Supported options:\n\
\n\
  -d <level>               debug level\n\
  -p <port>                UDP port to accept flows on (default %d)\n\
  -x <delay>               transmit delay in microseconds\n\
  -r                       use raw socket to maintain source addresses\n\
  -h                       print this usage message and exit\n\
\n\
Specifying receivers:\n\
\n\
  A.B.C.D[/port[/freq]]...\n\
where:
  A.B.C.D                  is the receiver's IP address\n\
  port                     is the UDP port to send to (default %d)\n\
  freq                     is the sampling rate (default 1)\n\
",
	   progname, FLOWPORT, FLOWPORT);
}

static int
send_pdu_to_peer (peer, fpdu, length, source_addr)
     struct peer * peer;
     const void * fpdu;
     size_t length;
     struct sockaddr_in * source_addr;
{
  if (peer->flags & pf_RAW)
    {
      return raw_send_from_to (peer->fd, fpdu, length,
			       source_addr, &peer->addr);
    }
  else
    {
      return sendto (peer->fd, (char*) fpdu, length, 0,
		     (struct sockaddr*) &peer->addr,
		     sizeof (struct sockaddr_in));
    }
}
