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
# ifndef HAVE_STRCHR
#  define strchr index
# endif
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
#  define memmove(d, s, n) bcopy ((s), (d), (n))
# endif
#endif
#ifdef HAVE_CTYPE_H
# include <ctype.h>
#endif
#ifndef HAVE_INET_ATON
extern int inet_aton (const char *, struct in_addr *);
#endif

#include "rawsend.h"

#define PDU_SIZE 1500

#define FLOWPORT 2000 

#define DEFAULT_SOCKBUFLEN 65536

#define PORT_SEPARATOR	':'
#define FREQ_SEPARATOR	'/'
#define TTL_SEPARATOR	','

#define MAX_PEERS 100
#define MAX_LINELEN 1000

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
  int			ttl;
  enum peer_flags	flags;
};

struct samplicator_context {
  struct source_context *sources;
  int			fport;
  long			sockbuflen;
  int			debug;
  enum peer_flags	defaultflags;

  int			fsockfd;
};


struct source_context {
  struct source_context *next;
  struct in_addr	source;
  struct in_addr	mask;
  struct peer	      *	peers;
  unsigned		npeers;
  unsigned		tx_delay;
  int			debug;
};

static void usage(const char *);
static int send_pdu_to_peer (struct peer *, const void *, size_t,
			     struct sockaddr_in *);
static void parse_args (int, char **, struct samplicator_context *, struct source_context *);
static int parse_peers (int, char **, struct samplicator_context *, struct source_context *);
static int init_samplicator (struct samplicator_context *);
static int samplicate (struct samplicator_context *);
static int make_cooked_udp_socket (long);
static void read_cf_file (const char *, struct samplicator_context *);

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
  struct samplicator_context ctx;
  struct source_context cmd_line;

  cmd_line.source.s_addr = 0;
  cmd_line.mask.s_addr = 0;
  cmd_line.npeers = 0;
  ctx.sources = &cmd_line;

  cmd_line.next = (struct source_context *) NULL;

  parse_args (argc, argv, &ctx, &cmd_line);

  if (init_samplicator (&ctx) == -1)
    exit (1);
  if (samplicate (&ctx) != 0) /* actually, samplicate() should never return. */
    exit (1);
  exit (0);
}

static void
parse_args (argc, argv, ctx, sctx)
     int argc;
     char **argv;
     struct samplicator_context *ctx;
     struct source_context *sctx;
{
  extern char *optarg;
  extern int errno, optind;
  int i;

  ctx->sockbuflen = DEFAULT_SOCKBUFLEN;
  ctx->fport = FLOWPORT;
  ctx->debug = 0;
  ctx->sources = NULL;
  /* assume that command-line supplied peers want to get all data */
  sctx->source.s_addr = 0;
  sctx->mask.s_addr = 0;

  sctx->tx_delay = 0;

  while ((i = getopt (argc, argv, "hb:d:p:x:c:S")) != -1)
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
      sctx->tx_delay = atoi (optarg);
      break;
    case 'S': /* spoof */
      ctx->defaultflags |= pf_SPOOF;
      break;
    case 'c': /* config file */
      read_cf_file(optarg,ctx);
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

  if (argc - optind > 0)
    {
      if (parse_peers (argc - optind, argv + optind, ctx, sctx) == -1)
	{
	  usage (argv[0]);
	  exit (1);
	}
    }
}

static int
parse_peers (argc, argv, ctx, sctx)
     int argc;
     char **argv;
     struct samplicator_context *ctx;
     struct source_context *sctx;
{
  int i;
  char tmp_buf[256];
  static int cooked_sock = -1;
  static int raw_sock = -1;
  char *c;
  struct source_context *ptr;

  /* allocate for argc peer entries */
  sctx->npeers = argc;

  if (!(sctx->peers = (struct peer*) malloc (sctx->npeers * sizeof (struct peer)))) {
    fprintf(stderr, "malloc(): failed.\n");
    return -1;
  }

  /* zero out malloc'd memory */
  bzero(sctx->peers, sctx->npeers*sizeof (struct peer));

  /* fill in peer entries */
  for (i = 0; i < argc; ++i)
    {
      sctx->peers[i].flags = ctx->defaultflags;
	
      if (strlen (argv[i]) > 255)
	{
	  fprintf (stderr, "ouch!\n");
	  return -1;
	}
      strcpy (tmp_buf, argv[i]);

      /* skip to end or port seperator */
      for (c = tmp_buf; (*c != PORT_SEPARATOR) && (*c); ++c);

      /* extract the port part */
      if (*c == PORT_SEPARATOR)
	{
	  *c = 0;
	  ++c;
	  sctx->peers[i].addr.sin_port = htons (atoi(c));
	}
      else 
	sctx->peers[i].addr.sin_port = htons (FLOWPORT);

      /* extract the frequency part */
      sctx->peers[i].freqcount = 0;
      sctx->peers[i].freq = 1;
      for (; (*c != FREQ_SEPARATOR) && (*c); ++c)
	if (*c == TTL_SEPARATOR) goto TTL;
      if (*c == FREQ_SEPARATOR)
	{
	  *c = 0;
	  ++c;
	  sctx->peers[i].freq = atoi(c);
	}

      /* printf("Frequency: %d\n", sctx->peers[i].freq); */

       /* extract the TTL part */
       for (; (*c != TTL_SEPARATOR) && (*c); ++c); 
TTL:   
       if ((*c == TTL_SEPARATOR) && (*(c+1) > 0))
        {
          *c = 0;
          ++c;
          sctx->peers[i].ttl = atoi (c);
        }
       else
        sctx->peers[i].ttl = DEFAULT_TTL; 

      /* extract the ip address part */
      if (inet_aton (tmp_buf, & sctx->peers[i].addr.sin_addr) == 0)
	{
	  fprintf (stderr, "parsing IP address (%s) failed\n", tmp_buf);
	  return -1;
	}

      sctx->peers[i].addr.sin_family = AF_INET;

      if (sctx->peers[i].flags & pf_SPOOF)
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
		  return -1;
		}
	    }
	  sctx->peers[i].fd = raw_sock;
	}
      else
	{
	  if (cooked_sock == -1)
	    {
	      if ((cooked_sock = make_cooked_udp_socket (ctx->sockbuflen)) < 0)
		{
		  fprintf (stderr, "creating cooked socket: %s\n",
			   strerror(errno));
		  return -1;
		}
	    }
	  sctx->peers[i].fd = cooked_sock;
	}
    }
    if (ctx->sources == NULL) {
	ctx->sources = sctx;
    } else {
    	for (ptr = ctx->sources; ptr->next != NULL; ptr = ptr->next);
	ptr->next = sctx;
    } 
  return 0;
}

static void
read_cf_file (file, ctx)
     const char *file;
     struct samplicator_context *ctx;
{
  FILE *cf;
  char tmp_s[MAX_LINELEN];
  int argc;
  char *argv[MAX_PEERS];
  unsigned char *c, *slash, *e;
  struct source_context *sctx;

  if ((cf = fopen(file,"r")) == NULL)
    {
      fprintf(stderr, "read_cf_file: cannot open %s. Aborting.\n",file);
      exit(1);
    }

  while (!feof(cf))
    {
      fgets(tmp_s, MAX_LINELEN - 1, cf);
	
      if ((c = strchr(tmp_s, '#')) != 0)
	continue;
	
      /* lines look like this:

      ipadd[/mask]: dest[:port[/freq][,ttl]]  dest2[:port2[/freq2][,ttl2]]...

      */
	
      if ((c = strchr(tmp_s, ':')) != 0)
	{
	  *c++ = 0;

	  sctx = calloc(1, sizeof(struct source_context));
	  if ((slash = strchr (tmp_s, '/')) != 0)
	    {
	      *slash++ = 0;
		
	      /* fprintf (stderr, "parsing IP address mask (%s)\n",slash); */
	      if (inet_aton (slash, &(sctx->mask)) == 0)
		{
             	  fprintf (stderr, "parsing IP address mask (%s) failed\n",slash);
            	  exit (1);
		}
	    }
	  else
	    {
	      inet_aton ("255.255.255.255", &(sctx->mask));
	    }
	  /* fprintf (stderr, "parsing IP address (%s)\n",tmp_s); */
  	  if (inet_aton (tmp_s, &(sctx->source)) == 0)
            {
	      fprintf (stderr, "parsing IP address (%s) failed\n",tmp_s);
	      exit (1);
	    }

	  /*	
	    fprintf (stderr, "parsed into %s/",
	    inet_ntoa(sctx->source));
	    fprintf (stderr, "%s\n",
	    inet_ntoa(sctx->mask));
	  */

	  argc = 0;
	  while (*c != 0)
	    {
	      while ((*c != 0) && isspace ((int) *c))
		c++;
	      if (*c == 0 ) break;

	      e = c;
	      while((*e != 0) && !isspace ((int) *e))
		e++;
	      argv[argc++] = c;
	      c = e;
	      if (*c != 0)
		c++;
	      *e = 0;
	    }
	  if (argc > 0) 
	      if (parse_peers (argc, argv, ctx, sctx) == -1) {
          	usage (argv[0]);
          	exit (1);
              }
	}
    }
  fclose(cf);
}

/* init_samplicator: prepares receiving socket */
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
    return -1;
  }
  if (setsockopt (ctx->fsockfd, SOL_SOCKET, SO_RCVBUF,
		  (char *) &ctx->sockbuflen, sizeof ctx->sockbuflen) == -1)
    {
      fprintf (stderr, "setsockopt(SO_RCVBUF,%ld): %s\n",
	       ctx->sockbuflen, strerror (errno));
    }
  if (bind (ctx->fsockfd,
	    (struct sockaddr*)&local_address, sizeof local_address) < 0) {
    fprintf (stderr, "bind(): %s\n", strerror (errno));
    return -1;
  }
  return 0;
}

static int
samplicate (ctx)
     struct samplicator_context *ctx;
{
  unsigned char fpdu[PDU_SIZE];
  struct sockaddr_in remote_address;
  struct source_context *sctx;
  int i, len, n;

  /* check is there actually at least one configured data receiver */
  for (i = 0, sctx = ctx->sources; sctx != NULL; sctx = sctx->next)
	if(sctx->npeers > 0)  i += sctx->npeers; 
  if (i == 0) {
        fprintf(stderr, "You have to specify at least one receiver, exiting\n");
        exit(1);
  }

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


      for(sctx = ctx->sources; sctx != NULL; sctx = sctx->next)
	{
	if ((sctx->source.s_addr == 0) || ((remote_address.sin_addr.s_addr & sctx->mask.s_addr) == sctx->source.s_addr))
	      for (i = 0; i < sctx->npeers; ++i)
	{
		  if (sctx->peers[i].freqcount == 0)
	    {
		      if (send_pdu_to_peer (& (sctx->peers[i]), fpdu, n, &remote_address)
		  == -1)
		{
		  fprintf (stderr, "sending datagram to %s:%d failed: %s\n",
				   inet_ntoa (sctx->peers[i].addr.sin_addr),
				   (int) ntohs (sctx->peers[i].addr.sin_port),
			   strerror (errno));
		}
	      else if (ctx->debug)
		{
		  fprintf (stderr, "  sent to %s:%d\n",
				   inet_ntoa (sctx->peers[i].addr.sin_addr),
				   (int) ntohs (sctx->peers[i].addr.sin_port)); 
		}
		      sctx->peers[i].freqcount = sctx->peers[i].freq-1;
	    }
	  else
	    {
		      --sctx->peers[i].freqcount;
		    }
		  if (sctx->tx_delay)
		    usleep (sctx->tx_delay);
		}
	else
		{
		if (ctx->debug)
			{
	  		fprintf (stderr, "Not matching %s/", inet_ntoa(sctx->source));
	  		fprintf (stderr, "%s\n", inet_ntoa(sctx->mask));
			}
	    }
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
  -p <port>                UDP port to accept flows on (default %d)\n\
  -d <level>               debug level\n\
  -b <size>                set socket buffer size (default %lu)\n\
  -S                       maintain (spoof) source addresses\n\
  -x <delay>               transmit delay in microseconds\n\
  -c configfile            specify a config file to read\n\
  -h                       print this usage message and exit\n\
\n\
Specifying receivers:\n\
\n\
  A.B.C.D[:port[/freq][,ttl]]...\n\
where:\n\
  A.B.C.D                  is the receiver's IP address\n\
  port                     is the UDP port to send to (default %d)\n\
  freq                     is the sampling rate (default 1)\n\
  ttl                      is the sending packets TTL value (default %d)\n\
\n\
Config file format:\n\
\n\
  a.b.c.d[/e.f.g.h]: receiver ...\n\
where:\n\
  a.b.c.d                  is the senders IP address\n\
  e.f.g.h                  is a mask to apply to the sender (default 255.255.255.255)\n\
  receiver                 see above.\n\
\n\
Receivers specified on the command line will get all packets, those\n\
specified in the config-file will get only packets with a matching source.\n\n\
",
	   progname,
	   FLOWPORT, (unsigned long) DEFAULT_SOCKBUFLEN,
	   FLOWPORT,
	   DEFAULT_TTL);
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
			       source_addr, &peer->addr, peer->ttl);
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
      if (setsockopt (s, SOL_SOCKET, SO_SNDBUF,
		      (char *) &sockbuflen, sizeof sockbuflen) == -1)
	{
	  fprintf (stderr, "setsockopt(SO_SNDBUF,%ld): %s\n",
		   sockbuflen, strerror (errno));
	}
    }
  return s;
}
