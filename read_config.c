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

#include "samplicator.h"
#include "read_config.h"
#include "inet.h"
#include "rawsend.h"

#define PORT_SEPARATOR	'/'
#define FREQ_SEPARATOR	'/'
#define TTL_SEPARATOR	','

#define FLOWPORT "2000"

#define DEFAULT_SOCKBUFLEN 65536
#define DEFAULT_PDULEN 65536

#define MAX_PEERS 100
#define MAX_LINELEN 8000

static int parse_line (struct samplicator_context *, char *, const char *);
static int parse_addr_mask (const char *, const char *,
			    const struct samplicator_context *,
			    struct sockaddr_storage *,
			    struct sockaddr_storage *,
			    socklen_t *);
static void short_usage (const char *);
static void usage (const char *);

static int
parse_error (const struct samplicator_context *ctx, const char *fmt, ...)
{
  va_list fmt_args;
  va_start (fmt_args, fmt);
  fprintf (stderr, "%s, line %d: ", ctx->config_file_name, ctx->config_file_lineno);
  vfprintf (stderr, fmt, fmt_args);
  fprintf (stderr, "\n");
  va_end (fmt_args);
  return -1;
}
  

/* copy_string_start_end (start, end)

   Copy a string defined by a start and end pointer to a fresh
   null-terminated string.  This is useful when using library
   functions that expect such strings.

   Will return a null pointer if allocating space fails.

   The copy should be free()'d when the caller is done with it.
 */
static char *
copy_string_start_end (start, end)
     const char *start;
     const char *end;
{
  size_t len = end-start;
  char *copy = malloc (len+1);
  if (copy == 0)
    return 0;
  strncpy (copy, start, len);
  copy[len] = 0;
  return copy;
}

/* read_cf_file (file, ctx)

   Read configuration file FILE into samplicator context CTX.

   The file is opened and parsed line by line.  The parser fills in
   values of CTX.

   The file name and a line counter are stored in CTX for use in
   parser error messages.

   Return value:

   0, if the file could be parsed.
   -1, if an error occurred during parsing.

   If an error occurs, the parser will give up immediately.  Contents
   preceding the errors may have been processed and filled into CTX.
 */
int
read_cf_file (file, ctx)
     const char *file;
     struct samplicator_context *ctx;
{
  FILE *cf;
  char tmp_s[MAX_LINELEN];
  ctx->config_file_name = file;
  ctx->config_file_lineno = 0;

  if ((cf = fopen (file,"r")) == NULL)
    {
      fprintf (stderr, "read_cf_file: cannot open %s. Aborting.\n", file);
      return -1;
    }
  while (!feof (cf))
    {
      if (fgets (tmp_s, MAX_LINELEN, cf) == (char *) 0)
	{
	  break;
	}
	
      ++ctx->config_file_lineno;
      if (parse_line (ctx, tmp_s, tmp_s + strlen (tmp_s)) == -1)
	{
	  return -1;
	}
    }
  fclose (cf);
  return 0;
}

/* resolve_addr (string, ctx, addrp, addrlenp)

   Parses, and possibly resolves, an IP address given as a string.

   The preferences in CTX are used to determine whether to return an
   IPv4 or IPv6 address.

   The address is stored in the sockaddr_storage structure pointed to
   by ADDRP.

   If ADDRLENP is non-null, it the length of the address structure
   will be stored to it.

   If an error occurs during parsing or resolution, the function will
   return -1, and nothing will be stored in ADDRP or ADDRLENP.
 */
static int
resolve_addr (const char *addrstring,
	      const struct samplicator_context *ctx,
	      struct sockaddr_storage *addrp,
	      socklen_t *addrlenp)
{
  struct addrinfo hints, *res;

  init_hints_from_preferences (&hints, ctx);

  if (getaddrinfo (addrstring, 0, &hints, &res) != 0 || res == 0)
    {
      return parse_error (ctx, "Could not parse address %s", addrstring);
    }
  memcpy (addrp, res->ai_addr, res->ai_addrlen);
  if (addrlenp != 0)
    *addrlenp = res->ai_addrlen;
  freeaddrinfo (res);
  return 0;
}

static int
parse_addr_1 (const char *start,
	      const char *end,
	      const struct samplicator_context *ctx,
	      struct sockaddr_storage *addrp,
	      socklen_t *addrlenp)
{
  char *copy = copy_string_start_end (start, end);
  int result;

  if (copy == 0)
    {
      return parse_error (ctx, "Out of memory");
    }
  result = resolve_addr (copy, ctx, addrp, addrlenp);
  free (copy);
  return result;
}

static int
parse_addr (const char *start,
	    const char *end,
	    const struct samplicator_context *ctx,
	    struct sockaddr_storage *addrp,
	    socklen_t *addrlenp)
{
  while (start < end && isspace (*start))
    ++start;
  while (start < end && isspace (*(end-1)))
    --end;
  if (start < end && *start == '[')
    {
      ++start;
      if (start < end && *(end-1) == ']')
	--end;
      else
	{
	  return parse_error (ctx, "Mismatched brackets");
	}
    }
  return parse_addr_1 (start, end, ctx, addrp, addrlenp);
}

static void
set_ipv4_netmask(struct sockaddr_in *maskp, int preflen)
{
  in_addr_t *addrp = &maskp->sin_addr.s_addr;
  int k;
  unsigned long bit;
  uint32_t haddr;

  haddr = 0xffffffff;
  preflen = 32-preflen;
  for (k = 0, bit = 1; k < preflen; ++k, bit <<= 1)
    {
      haddr &= ~bit;
    }
  *addrp = htonl (haddr);
}

static void
set_ipv6_netmask(struct sockaddr_in6 *maskp, int preflen)
{
  uint8_t *addrp = maskp->sin6_addr.s6_addr;
  int k;
  unsigned bit;
  unsigned octet_index;

  memset (addrp, 0, 16);
  for (k = 0, bit = 128, octet_index = 0;
       k < preflen;
       ++k, bit >>= 1)
    {
      if (bit < 1)
	{
	  bit = 128, octet_index++;
	}
      addrp[octet_index] |= bit;
    }
}

static int
parse_mask (const char *start,
	    const char *end,
	    const struct samplicator_context *ctx,
	    struct sockaddr_storage *maskp,
	    struct sockaddr_storage *addrp)
{
  if (addrp->ss_family == AF_INET)
    {
      const char *cp = start;
      while (cp < end && *cp != '.')
	++cp;
      if (cp < end)		/* contains a dot, assume full netmask */
	{
	  socklen_t addrlen1;
	  if (parse_addr_1 (start, end, ctx, maskp, &addrlen1) != 0)
	    {
	      return parse_error (ctx, "Cannot parse mask");
	    }
	  else
	    {
	      if (addrlen1 != sizeof (struct sockaddr_in))
		{
		  return parse_error (ctx, "Inconsistent addr/mask structures");
		}
	      return 0;
	    }
	}
      else
	{			/* no dot, assume this is a prefix length */
	  int preflen;
	  char *int_end;
	  preflen = strtol (start, &int_end, 10);
	  if (int_end == start || int_end < end || preflen < 0 || preflen > 32)
	    {
	      return parse_error (ctx, "Bogus prefix length");
	    }
	  bzero (maskp, sizeof (struct sockaddr_in));
	  ((struct sockaddr_in *)maskp)->sin_family = AF_INET;
	  set_ipv4_netmask((struct sockaddr_in *)maskp, preflen);
	  return 0;
	}
    }
  else if (addrp->ss_family == AF_INET6)
    {
      int preflen;
      char *int_end;
      preflen = strtol (start, &int_end, 10);
      if (int_end == start || int_end < end || preflen < 0 || preflen > 128)
	{
	  return parse_error (ctx, "Bogus prefix length");
	}
      bzero (maskp, sizeof (struct sockaddr_in6));
      ((struct sockaddr_in6 *)maskp)->sin6_family = AF_INET6;
      set_ipv6_netmask((struct sockaddr_in6 *)maskp, preflen);
      return 0;
    }
  else
    {
      return parse_error (ctx, "Unsupported address family");
    }
}

static int
set_default_mask (const struct samplicator_context *ctx,
		  struct sockaddr_storage *maskp,
		  struct sockaddr_storage *addrp)
{
  if (addrp->ss_family == AF_INET)
    {
      bzero (maskp, sizeof (struct sockaddr_in));
      maskp->ss_family = AF_INET;
      memset (&((struct sockaddr_in *) maskp)->sin_addr, 0xff, 4);
      return 0;
    }
  else if (addrp->ss_family == AF_INET6)
    {
      bzero (maskp, sizeof (struct sockaddr_in6));
      maskp->ss_family = AF_INET6;
      memset (&((struct sockaddr_in6 *) maskp)->sin6_addr, 0xff, 16);
      return 0;
    }
  return parse_error (ctx, "Unknown address family %d", addrp->ss_family);
}

static int
parse_addr_mask (start, end, ctx, addrp, maskp, addrlenp)
     const char *start, *end;
     const struct samplicator_context *ctx;
     struct sockaddr_storage *addrp;
     struct sockaddr_storage *maskp;
     socklen_t *addrlenp;
{
  const char *c, *slash = 0;

  c = start;
  while (c < end && *c != '/')
    ++c;
  if (c < end)
    slash = c;

  if (parse_addr (start, slash == 0 ? end : slash, ctx, addrp, addrlenp) == -1)
    return -1;

  if (slash != 0)
    {
      if (parse_mask (slash+1, end, ctx, maskp, addrp) == -1)
	return -1;
    }
  else
    set_default_mask (ctx, maskp, addrp);
  return 0;
}

/*
  parse_line (ctx, start, end)

  Parse a single line of configuration file.

*/
static int
parse_line (ctx, start, end)
     struct samplicator_context *ctx;
     char *start;
     const char *end;
{
  struct source_context *sctx;
  int argc;
  const char *argv[MAX_PEERS];
  const char *c, *e;
  const char *lhs_start, *lhs_end;
  const char *rhs_start;

  /* move end before the start of any comment at the end of the line,
     and before any whitepace preceding such a comment or EOL. */
  for (c = start; c < end && *c != '#'; ++c) ;
  if (c < end)
    end = c;
  while (end > start && isspace (*(end-1)))
    --end;
  if (start == end)
    return 0;			/* empty line; skip. */

  /* non-empty lines should look like this:

     ipadd[/mask]: dest[:port[/freq][,ttl]]  dest2[:port2[/freq2][,ttl2]]...

     The problematic case is where the ipadd is an IPv6 address,
     because IPv6 addresses usually contain colons.  We insist on the
     convention that IPv6 addresses be enclosed in brackets.  In
     addition, for IPv6 we only accept prefix lengths, not arbitrary
     netmasks.

       [2001:db0:0:1::]/64: [2001:db0:0:2::3]:8000 [2001:db0:0:2::4]:8000
  */

  c = start;
  while (c < end && isspace (*c))
    ++c;
  lhs_start = c;
  if (*c == '[') {
    while (c < end && *c != ']')
      ++c;
    while (c < end && *c != ':')
      ++c;
  } else {
    c = start;
    while (c < end && *c != ':')
      ++c;
  }
  /* Now c either points at the colon that separates the left-hand
     address/mask from the right hand destinations, or c is equal to
     end because such a colon was not found. */

  if (c < end)
    {
      lhs_end = c;
      ++c;			/* skip colon */
      while (c < end && isspace (*c))
	++c;
      rhs_start = c;
      sctx = calloc (1, sizeof (struct source_context));

      if (parse_addr_mask (lhs_start, lhs_end, ctx,
			   &sctx->source, &sctx->mask, &sctx->addrlen) != 0)
	return -1;

      argc = 0;
      while (c < end)
	{
	  while (c < end && isspace (*c))
	    c++;
	  if (c >= end) break;
	  e = c;
	  while((*e != 0) && !isspace ((int) *e))
	    e++;
	  argv[argc++] = copy_string_start_end (c, e);
	  c = e;
	  if (c < end)
	    c++;
	}
      if (argc > 0) 
	{
	  if (parse_receivers (argc, argv, ctx, sctx) == -1)
	    {
	      return -1;
	    }
	}
    }
  else
    {
      return parse_error (ctx, "Missing colon");
    }
  return 0;
}

static int
parse_receiver (struct receiver *receiverp,
		const char *arg,
		struct samplicator_context *ctx)
{
  const char *start, *end;
  const char *host_start, *host_end;
  char portspec[NI_MAXSERV];
  struct addrinfo hints, *res;
  int result;

  receiverp->flags = ctx->default_receiver_flags;
  receiverp->freqcount = 0;
  receiverp->freq = 1;
  receiverp->ttl = DEFAULT_TTL; 

  start = arg; end = start + strlen (arg);
  while (start < end && isspace (*start))
    ++start;
  while (start < end && isspace (*(end-1)))
    --end;

  if (start < end && *start == '[')
    {
      host_end = host_start = start+1;
      while (host_end < end && *host_end != ']')
	++host_end;
      if (host_end == end)
	{
	  return parse_error (ctx, "Missing closing bracket");
	}
      start = host_end+1;
    }
  else
    {
      host_end = host_start = start;
      while (host_end < end && *host_end != PORT_SEPARATOR)
	++host_end;
      start = host_end;
    }

  /* extract the port part */
  if (*start == PORT_SEPARATOR)
    {
      const char *port_start, *port_end;

      ++start;
      port_end = port_start = start;
      while (port_end < end && *port_end != FREQ_SEPARATOR)
	++port_end;
      if (port_end < end)
	{
	  const char *freq_start, *freq_end;
	  freq_end = freq_start = port_end + 1;

	  /* extract the frequency part */
	  while (freq_end < end && *freq_end != TTL_SEPARATOR)
	    ++freq_end;

	  if (freq_start < freq_end)
	    {
	      int freq;
	      char *freq_parse_end;

	      freq = strtol (freq_start, &freq_parse_end, 10);
	      if (freq_parse_end == freq_start
		  || freq_parse_end != freq_end
		  || freq < 1)
		{
		  return parse_error (ctx, "Illegal frequency .*s",
				      freq_end-freq_start, freq_start);
		}
	      else
		{
		  receiverp->freq = freq;
		}
	    }
	  if (freq_end < end)
	    {
	      int ttl;
	      const char *ttl_start = freq_end + 1;
	      const char *ttl_end = end;
	      char *ttl_parse_end;

	      ttl = strtol (ttl_start, &ttl_parse_end, 10);
	      if (ttl_parse_end == ttl_start
		  || ttl_parse_end != ttl_end
		  || ttl < 1 || ttl > 255)
		{
		  return parse_error (ctx, "Illegal TTL");
		}
	      else
		{
		  receiverp->ttl = ttl;
		}
	    }
	}
      if (port_end - port_start >= NI_MAXSERV)
	{
	  return parse_error (ctx, "Service name/port number (%.*s) too long",
			      port_end-port_start, port_start);
	}
      strncpy (portspec, port_start, port_end-port_start);
      portspec[port_end-port_start] = 0;
    }
  else
    strcpy (portspec, FLOWPORT);

  init_hints_from_preferences (&hints, ctx);
  {
    char *tmp_buf = copy_string_start_end (host_start, host_end);
    if (tmp_buf == 0)
      {
	return parse_error (ctx, "Out of memory");
      }
    result = getaddrinfo (tmp_buf, portspec, &hints, &res);
    if (result != 0)
      {
	return parse_error (ctx, "Parsing IP address (%s with port spec %s) failed: %s",
			    tmp_buf, portspec, gai_strerror (result));
      }
    memcpy (&receiverp->addr, res->ai_addr, res->ai_addrlen);
    receiverp->addrlen = res->ai_addrlen;
    freeaddrinfo(res);
    return 0;
  }
}

int
parse_receivers (argc, argv, ctx, sctx)
     int argc;
     const char **argv;
     struct samplicator_context *ctx;
     struct source_context *sctx;
{
  int i;

  /* allocate for argc receiver entries */
  sctx->nreceivers = argc;

  if (!(sctx->receivers = (struct receiver*) calloc (sctx->nreceivers, sizeof (struct receiver)))) {
    return parse_error (ctx, "Out of memory");
  }

  /* fill in receiver entries */
  for (i = 0; i < argc; ++i)
    {
      if (parse_receiver (&sctx->receivers[i], argv[i], ctx) != 0)
	{
	  return -1;
	}
    }
  if (ctx->sources == NULL)
    {
      ctx->sources = sctx;
    }
  else
    {
      struct source_context *ptr;
      for (ptr = ctx->sources; ptr->next != NULL; ptr = ptr->next);
      ptr->next = sctx;
    } 
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

  ctx->config_file_name = "<command line>";
  ctx->config_file_lineno = 1;
  sctx->nreceivers = 0;
  ctx->sources = sctx;

  sctx->next = (struct source_context *) NULL;

  ctx->sockbuflen = DEFAULT_SOCKBUFLEN;
  ctx->pdulen = DEFAULT_PDULEN;
  ctx->faddr_spec = 0;
  bzero (&ctx->faddr, sizeof ctx->faddr);
  ctx->fport_spec = FLOWPORT;
  ctx->debug = 0;
  ctx->ipv4_only = 0;
  ctx->ipv6_only = 0;
  ctx->fork = 0;
  ctx->pid_file = (const char *) 0;
  ctx->sources = 0;
  ctx->default_receiver_flags = pf_CHECKSUM;
  /* assume that command-line supplied receivers want to get all data */
  sctx->source.ss_family = AF_INET;
  ((struct sockaddr_in *) &sctx->source)->sin_addr.s_addr = 0;
  ((struct sockaddr_in *) &sctx->mask)->sin_addr.s_addr = 0;

  sctx->tx_delay = 0;

  optind = 1;
  while ((i = getopt (argc, (char **) argv, "hu:b:d:m:p:s:x:c:fSn46")) != -1)
    {
      switch (i)
	{
	case 'b': /* buflen */
	  ctx->sockbuflen = atol (optarg);
	  break;
	case 'u': /* pdu length */
	  ctx->pdulen = atol (optarg);
	  break;
	case 'd': /* debug */
	  ctx->debug = atoi (optarg);
	  break;
	case 'n': /* no UDP checksums */
	  ctx->default_receiver_flags &= ~pf_CHECKSUM;
	  break;
	case 'p': /* flow port */
	  ctx->fport_spec = optarg;
	  break;
	case 'm': /* make PID file */
	  ctx->pid_file = optarg;
	  break;
	case 's': /* flow address */
	  ctx->faddr_spec = optarg;
	  break;
	case 'x': /* transmit delay */
	  sctx->tx_delay = atoi (optarg);
	  break;
	case 'S': /* spoof */
	  ctx->default_receiver_flags |= pf_SPOOF;
	  break;
	case 'c': /* config file */
	  if (read_cf_file (optarg, ctx) != 0)
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
	case '4':
	  ctx->ipv6_only = 0;
	  ctx->ipv4_only = 1;
	  break;
	case '6':
	  ctx->ipv4_only = 0;
	  ctx->ipv6_only = 1;
	  break;
	default:
	  short_usage (argv[0]);
	  return -1;
	}
    }

  if (argc - optind > 0)
    {
      if (parse_receivers (argc - optind, argv + optind, ctx, sctx) == -1)
	{
	  short_usage (argv[0]);
	  return -1;
	}
    }
  return 0;
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
  -p <port>                UDP port to accept flows on (default %s)\n\
  -s <address>             Interface address to accept flows on (default any)\n\
  -d <level>               debug level\n\
  -b <size>                set socket buffer size (default %lu)\n\
  -n			   don't compute UDP checksum (leave at 0)\n\
  -S                       maintain (spoof) source addresses\n\
  -x <delay>               transmit delay in microseconds\n\
  -c <configfile>          specify a config file to read\n\
  -f                       fork program into background\n\
  -m <pidfile>             write process ID to file\n\
  -4                       IPv4 only\n\
  -6                       IPv6 only\n\
  -h                       print this usage message and exit\n\
  -u <pdulen>              size of max pdu on listened socket (default 65536)\n\
\n\
Specifying receivers:\n\
\n\
  A.B.C.D[%cport[%cfreq][%cttl]]...\n\
where:\n\
  A.B.C.D                  is the receiver's IP address\n\
  port                     is the UDP port to send to (default %s)\n\
  freq                     is the sampling rate (default 1)\n\
  ttl                      is the outgoing packets' TTL value (default %d)\n\
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
