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
#ifndef HAVE_INET_ATON
extern int inet_aton (const char *, struct in_addr *);
#endif

#include "rawsend.h"

#define PDU_SIZE 1500

#define FLOWPORT 2000 

enum peer_flags
{
  pf_SPOOF	= 0x0001,
};

struct peer {
  int			fd;
  struct sockaddr_in	addr;
  int			port;
  int			freq;
  int			freqcount;
  enum peer_flags	flags;
};

struct samplicator_context {
  struct peer	      *	peers;
  unsigned		npeers;
  int			fport;
  unsigned		tx_delay;
  long			sockbuflen;
  int			debug;

  int			fsockfd;
};

static void usage(const char *);
static int send_pdu_to_peer (struct peer *, const void *, size_t,
			     struct sockaddr_in *);
static int parse_args (int, char **, struct samplicator_context *);
static int init_samplicator (struct samplicator_context *);
static int samplicate (struct samplicator_context *);
static int make_cooked_udp_socket (long);

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
  struct peer * peers;
  int npeers;
  int fport;
  unsigned tx_delay;
  long sockbuflen;
  struct samplicator_context ctx;

  parse_args (argc, argv, &ctx);
  init_samplicator (&ctx);
  samplicate (&ctx);
}

static int
parse_args (argc, argv, ctx)
     int argc;
     char **argv;
     struct samplicator_context *ctx;
{
  extern char *optarg;
  extern int errno, optind;
  char tmp_buf[256];
  char *c;
  int spoof_p = 0;
  int raw_sock = -1;
  int cooked_sock = -1;
  int i, n;

  ctx->sockbuflen = -1;
  ctx->fport = FLOWPORT;
  ctx->debug = 0;
  ctx->tx_delay = 0;

  while ((i = getopt (argc, argv, "hb:d:p:x:S")) != -1)
    switch (i) {
    case 'b': /* buflen */
      ctx->sockbuflen = atol (optarg);
      break;
    case 'd': /* debug */
      ctx->debug = atoi (optarg);
      break;
    case 'p': /* flow Port */
      ctx->fport = atoi (optarg);
      break;
    case 'x': /* transmit delay */
      ctx->tx_delay = atoi (optarg);
      break;
    case 'S': /* spoof */
      spoof_p = 1;
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
  ctx->npeers = argc - optind;

  if (!(ctx->peers = (struct peer*) malloc (ctx->npeers * sizeof (struct peer)))) {
    fprintf(stderr, "malloc(): failed.\n");
    exit (1);
  }

  /* zero out malloc'd memory */
  bzero(ctx->peers, ctx->npeers*sizeof (struct peer));

  /* fill in peer entries */
  for (i = optind, n = 0; i < argc; ++i, ++n)
    {
      if (spoof_p)
	{
	  ctx->peers[n].flags |= pf_SPOOF;
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
	  ctx->peers[n].addr.sin_port = htons (atoi(c));
	}
      else 
	ctx->peers[n].addr.sin_port = htons (FLOWPORT);

      /* extract the frequency part */
      for (; (*c != '/') && (*c); ++c)
	;
      if (*c == '/')
	{
	  *c = 0;
	  ++c;
	  ctx->peers[n].freq = atoi(c);
	}
      else
	ctx->peers[n].freq = 1;
      ctx->peers[n].freqcount = 0;

      /* printf("Frequency: %d\n", ctx->peers[n].freq); */

      /* extract the ip address part */
      if (inet_aton (tmp_buf, & ctx->peers[n].addr.sin_addr) == 0)
	{
	  fprintf (stderr, "parsing IP address failed\n");
	  exit (1);
	}

      ctx->peers[n].addr.sin_family = AF_INET;

      if (ctx->peers[n].flags & pf_SPOOF)
	{
	  if (raw_sock == -1)
	    {
	      if ((raw_sock = make_raw_udp_socket (ctx->sockbuflen)) < 0)
		{
		  if (errno == EPERM)
		    {
		      fprintf (stderr, "Not enough privilege for -S option---try again as root.\n");
		    }
		  else
		    {
		      fprintf (stderr, "creating raw socket: %s\n", strerror(errno));
		    }
		  exit (1);
		}
	    }
	  ctx->peers[n].fd = raw_sock;
	}
      else
	{
	  if (cooked_sock == -1)
	    {
	      if ((cooked_sock = make_cooked_udp_socket (ctx->sockbuflen)) < 0)
		{
		  fprintf (stderr, "creating cooked socket: %s\n", strerror(errno));
		  exit (1);
		}
	    }
	  ctx->peers[n].fd = cooked_sock;
	}
    }
}

static int
init_samplicator (ctx)
     struct samplicator_context *ctx;
{
  struct sockaddr_in local_address;

  /* setup to receive flows */
  bzero (&local_address, sizeof local_address);
  local_address.sin_family = AF_INET;
  local_address.sin_addr.s_addr = htonl (INADDR_ANY);
  local_address.sin_port = htons (ctx->fport);

  if ((ctx->fsockfd = socket (AF_INET, SOCK_DGRAM, 0)) < 0) {
    fprintf (stderr, "socket(): %s\n", strerror (errno));
    exit(1);
  }
  if (bind (ctx->fsockfd, (struct sockaddr*)&local_address, sizeof local_address) < 0) {
    fprintf (stderr, "bind(): %s\n", strerror (errno));
    exit (1);
  }
}

static int
samplicate (ctx)
     struct samplicator_context *ctx;
{
  unsigned char fpdu[PDU_SIZE];
  struct sockaddr_in remote_address;
  int i, len, n;

  while (1)
    {
      len = sizeof remote_address;
      if ((n = recvfrom (ctx->fsockfd, (char*)fpdu,
			 sizeof (fpdu), 0,
			 (struct sockaddr*) &remote_address, &len)) == -1)
	{
	  fprintf(stderr, "recvfrom(): %s\n", strerror(errno));
	  exit (1);
	}
      if (n > PDU_SIZE)
	{
	  fprintf (stderr, "Warning: %d excess bytes discarded\n",
		   n-PDU_SIZE);
	  n = PDU_SIZE;
	}
      if (len != sizeof remote_address)
	{
	  fprintf (stderr, "recvfrom() return address length %d - expected %d\n",
		   len, sizeof remote_address);
	  exit (1);
	}
      if (ctx->debug)
	{
	  fprintf (stderr, "received %d bytes from %s:%d\n",
		   n,
		   inet_ntoa (remote_address.sin_addr),
		   (int) ntohs (remote_address.sin_port));
	}
      for (i = 0; i < ctx->npeers; ++i)
	{
	  if (ctx->peers[i].freqcount == 0)
	    {
	      if (send_pdu_to_peer (& (ctx->peers[i]), fpdu, n, &remote_address)
		  == -1)
		{
		  fprintf (stderr, "sending datagram failed: %s\n",
			   inet_ntoa (ctx->peers[i].addr.sin_addr),
			   (int) ntohs (ctx->peers[i].addr.sin_port),
			   strerror (errno));
		}
	      else if (ctx->debug)
		{
		  fprintf (stderr, "  sent to %s:%d\n",
			   inet_ntoa (ctx->peers[i].addr.sin_addr),
			   (int) ntohs (ctx->peers[i].addr.sin_port)); 
		}
	      ctx->peers[i].freqcount = ctx->peers[i].freq-1;
	    }
	  else
	    {
	      --ctx->peers[i].freqcount;
	    }
	  if (ctx->tx_delay)
	    usleep (ctx->tx_delay);
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
  -S                       maintain (spoof) source addresses\n\
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
  if (peer->flags & pf_SPOOF)
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

static int
make_cooked_udp_socket (sockbuflen)
     long sockbuflen;
{
  int s;
  if ((s = socket (PF_INET, SOCK_DGRAM, 0)) == -1)
    return s;
  if (sockbuflen != -1)
    {
      if (setsockopt (s, SOL_SOCKET, SO_RCVBUF, &sockbuflen, sizeof sockbuflen) == -1)
	{
	  fprintf (stderr, "setsockopt(SO_RCVBUF,%ld): %s\n",
		   sockbuflen, strerror (errno));
	}
      if (setsockopt (s, SOL_SOCKET, SO_SNDBUF, &sockbuflen, sizeof sockbuflen) == -1)
	{
	  fprintf (stderr, "setsockopt(SO_SNDBUF,%ld): %s\n",
		   sockbuflen, strerror (errno));
	}
    }
}
