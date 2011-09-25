/*
 read_config.c

 Date Created: Sun Sep 18 22:03:40 2011
 Author:       Simon Leinen  <simon.leinen@switch.ch>
 */

#include "config.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <stdarg.h>
#include <sys/types.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <sys/socket.h>
#include <netinet/in.h>
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#if STDC_HEADERS
# define bzero(b,n) memset(b,0,n)
#else
# include <strings.h>
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
# endif
#endif
#ifdef HAVE_CTYPE_H
# include <ctype.h>
#endif
#ifndef HAVE_INET_ATON
extern int inet_aton (const char *, struct in_addr *);
#endif

#include "samplicator.h"
#include "read_config.h"
#include "rawsend.h"

static int make_cooked_udp_socket (long);
static void usage (const char *);

#define PORT_SEPARATOR	'/'
#define FREQ_SEPARATOR	'/'
#define TTL_SEPARATOR	','

#define FLOWPORT 2000

#define DEFAULT_SOCKBUFLEN 65536

#define MAX_PEERS 100
#define MAX_LINELEN 8000

int
read_cf_file (file, ctx)
     const char *file;
     struct samplicator_context *ctx;
{
  FILE *cf;
  char tmp_s[MAX_LINELEN];
  int argc;
  const char *argv[MAX_PEERS];
  char *c, *slash, *e;
  struct source_context *sctx;

  if ((cf = fopen(file,"r")) == NULL)
    {
      fprintf(stderr, "read_cf_file: cannot open %s. Aborting.\n",file);
      exit(1);
    }
  while (!feof(cf))
    {
      if (fgets (tmp_s, MAX_LINELEN - 1, cf) == (char *) 0)
	{
	  break;
	}
	
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

	      ((struct sockaddr_in *) &sctx->mask)->sin_family = AF_INET;
	      /* fprintf (stderr, "parsing IP address mask (%s)\n",slash); */
	      if (inet_aton (slash, &(((struct sockaddr_in *) &sctx->mask)->sin_addr)) == 0)
		{
             	  fprintf (stderr, "parsing IP address mask (%s) failed\n",slash);
            	  exit (1);
		}
	    }
	  else
	    {
	      ((struct sockaddr_in *) &sctx->mask)->sin_family = AF_INET;
	      inet_aton ("255.255.255.255", &(((struct sockaddr_in *) &sctx->mask)->sin_addr));
	    }
	  /* fprintf (stderr, "parsing IP address (%s)\n",tmp_s); */
	  ((struct sockaddr_in *) &sctx->source)->sin_family = AF_INET;
  	  if (inet_aton (tmp_s, &((struct sockaddr_in *) &sctx->source)->sin_addr) == 0)
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
	    {
	      if (parse_receivers (argc, argv, ctx, sctx) == -1)
		{
		  usage (argv[0]);
		  exit (1);
		}
	    }
	}
    }
  fclose (cf);
  return 0;
}

int
parse_args (argc, argv, ctx)
     int argc;
     const char **argv;
     struct samplicator_context *ctx;
{
  extern char *optarg;
  extern int errno, optind;
  int i;
  struct source_context *sctx = calloc (1, sizeof (struct source_context));

  if (sctx == 0)
    {
      fprintf (stderr, "Out of memory\n");
      return -1;
    }

  sctx->nreceivers = 0;
  ctx->sources = sctx;

  sctx->next = (struct source_context *) NULL;

  ctx->sockbuflen = DEFAULT_SOCKBUFLEN;
  ctx->faddr.s_addr = htonl (INADDR_ANY);
  ctx->fport = FLOWPORT;
  ctx->debug = 0;
  ctx->fork = 0;
  ctx->sources = 0;
  ctx->defaultflags = pf_CHECKSUM;
  /* assume that command-line supplied receivers want to get all data */
  ((struct sockaddr_in *) &sctx->source)->sin_family = AF_INET;
  ((struct sockaddr_in *) &sctx->source)->sin_addr.s_addr = 0;
  ((struct sockaddr_in *) &sctx->mask)->sin_family = AF_INET;
  ((struct sockaddr_in *) &sctx->mask)->sin_addr.s_addr = 0;

  sctx->tx_delay = 0;

  optind = 1;
  while ((i = getopt (argc, (char **) argv, "hb:d:p:s:x:c:fSn")) != -1)
    {
      switch (i)
	{
	case 'b': /* buflen */
	  ctx->sockbuflen = atol (optarg);
	  break;
	case 'd': /* debug */
	  ctx->debug = atoi (optarg);
	  break;
	case 'n': /* no UDP checksums */
	  ctx->defaultflags &= ~pf_CHECKSUM;
	  break;
	case 'p': /* flow port */
	  ctx->fport = atoi (optarg);
	  if (ctx->fport < 0
	      || ctx->fport > 65535)
	    {
	      fprintf (stderr,
		       "Illegal receive port %d - \
should be between 0 and 65535\n",
		       ctx->fport);
	      usage (argv[0]);
	      exit (1);
	    }
	  break;
	case 's': /* flow address */
	  if (inet_aton (optarg, &ctx->faddr) == 0)
	    {
	      fprintf (stderr, "parsing IP address (%s) failed\n", optarg);
	      usage (argv[0]);
	      exit (1);
	    }
	  break;
	case 'x': /* transmit delay */
	  sctx->tx_delay = atoi (optarg);
	  break;
	case 'S': /* spoof */
	  ctx->defaultflags |= pf_SPOOF;
	  break;
	case 'c': /* config file */
	  if (read_cf_file(optarg, ctx) != 0)
	    {
	      return -1;
	    }
	  break;
	case 'f': /* fork */
	  ctx->fork = 1;
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
    }

  if (argc - optind > 0)
    {
      if (parse_receivers (argc - optind, argv + optind, ctx, sctx) == -1)
	{
	  usage (argv[0]);
	  exit (1);
	}
    }
  return 0;
}

int
parse_receivers (argc, argv, ctx, sctx)
     int argc;
     const char **argv;
     struct samplicator_context *ctx;
     struct source_context *sctx;
{
  int i;
  char tmp_buf[256];
  static int cooked_sock = -1;
  static int raw_sock = -1;
  char *c;
  struct source_context *ptr;

  /* allocate for argc receiver entries */
  sctx->nreceivers = argc;

  if (!(sctx->receivers = (struct receiver*) calloc (sctx->nreceivers, sizeof (struct receiver)))) {
    fprintf(stderr, "calloc(): failed.\n");
    return -1;
  }

  /* fill in receiver entries */
  for (i = 0; i < argc; ++i)
    {
      sctx->receivers[i].flags = ctx->defaultflags;
      sctx->receivers[i].addrlen = sizeof (struct sockaddr_in);
	
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
	  int port;
	  *c = 0;
	  ++c;
	  port = atoi(c);
	  if (port < 0 || port > 65535)
	    {
	      fprintf (stderr, "Illegal destination port %d - \
should be between 0 and 65535\n", port);
	      return -1;
	    }
	  ((struct sockaddr_in *) &sctx->receivers[i].addr)->sin_port = htons (port);
	}
      else 
	((struct sockaddr_in *) &sctx->receivers[i].addr)->sin_port = htons (FLOWPORT);

      /* extract the frequency part */
      sctx->receivers[i].freqcount = 0;
      sctx->receivers[i].freq = 1;
      for (; (*c != FREQ_SEPARATOR) && (*c); ++c)
	if (*c == TTL_SEPARATOR) goto TTL;
      if (*c == FREQ_SEPARATOR)
	{
	  *c = 0;
	  ++c;
	  sctx->receivers[i].freq = atoi(c);
	}

      /* printf("Frequency: %d\n", sctx->receivers[i].freq); */

      /* extract the TTL part */
      for (; (*c != TTL_SEPARATOR) && (*c); ++c); 
    TTL:   
      if ((*c == TTL_SEPARATOR) && (*(c+1) > 0))
        {
          *c = 0;
          ++c;
          sctx->receivers[i].ttl = atoi (c);
	  if (sctx->receivers[i].ttl < 1
	      || sctx->receivers[i].ttl > 255)
	    {
	      fprintf (stderr,
		       "Illegal value %d for TTL - should be between 1 and 255.\n",
		       sctx->receivers[i].ttl);
	      return -1;
	    }
        }
      else
        sctx->receivers[i].ttl = DEFAULT_TTL; 

      /* extract the ip address part */
      if (inet_aton (tmp_buf, & ((struct sockaddr_in *) &sctx->receivers[i].addr)->sin_addr) == 0)
	{
	  fprintf (stderr, "parsing IP address (%s) failed\n", tmp_buf);
	  return -1;
	}

      sctx->receivers[i].addrlen = sizeof (struct sockaddr_in);
      sctx->receivers[i].addr.ss_family = AF_INET;

      if (sctx->receivers[i].flags & pf_SPOOF)
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
	  sctx->receivers[i].fd = raw_sock;
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
	  sctx->receivers[i].fd = cooked_sock;
	}
    }
  if (ctx->sources == NULL)
    {
      ctx->sources = sctx;
    }
  else
    {
      for (ptr = ctx->sources; ptr->next != NULL; ptr = ptr->next);
      ptr->next = sctx;
    } 
  return 0;
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

void short_usage (progname)
     const char *progname;
{
  fprintf (stderr, "Run \"%s -h\" for usage information.\n", progname);
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
  -s <address>             Interface address to accept flows on (default any)\n\
  -d <level>               debug level\n\
  -b <size>                set socket buffer size (default %lu)\n\
  -n			   don't compute UDP checksum (leave at 0)\n\
  -S                       maintain (spoof) source addresses\n\
  -x <delay>               transmit delay in microseconds\n\
  -c configfile            specify a config file to read\n\
  -f                       fork program into background\n\
  -h                       print this usage message and exit\n\
\n\
Specifying receivers:\n\
\n\
  A.B.C.D[%cport[%cfreq][%cttl]]...\n\
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
	   PORT_SEPARATOR, FREQ_SEPARATOR, TTL_SEPARATOR,
	   FLOWPORT,
	   DEFAULT_TTL);
}
