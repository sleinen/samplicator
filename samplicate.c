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
#if STDC_HEADERS
# define bzero(b,n) memset(b,0,n)
#else
# include <strings.h>
# ifndef HAVE_STRCHR
#  define strchr index
#  define strrchr rindex
# endif
char *strchr (), *strrchr ();
# ifndef HAVE_MEMCPY
#  define memcpy(d, s, n) bcopy ((s), (d), (n))
#  define memmove(d, s, n) bcopy ((s), (d), (n))
# endif
#endif

#define PDU_SIZE 1500

#define FLOWPORT 2000 

static int debug;

static void usage(const char *);

struct peer {
  int			fd;
  struct sockaddr_in	addr;
  int			port;
  int			freq;
  int			freqcount;
};

static in_addr_t
scan_ip(const char *s)
{
  in_addr_t addr = 0;
  unsigned n;
 
  while (1) {
 
    /* n is the nibble */
    n = 0;
 
    /* nibble's are . bounded */
    while (*s && *s != '.')
      n = n * 10 + *s++ - '0';
 
    /* shift in the nibble */
    addr <<=8;
    addr |= n & 0xff;
 
    /* return on end of string */
    if (!*s)
      return addr;
 
    /* skip the . */
    ++s;
  } /* forever */
} /* scan_ip */

/* Work around a GCC compatibility problem with respect to the
   inet_ntoa() system function */
#undef inet_ntoa
#define inet_ntoa(x) my_inet_ntoa(&(x))

static const char *
my_inet_ntoa (const struct in_addr *in)
{
  unsigned a = in->s_addr;
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

  fport = FLOWPORT;
  debug = 0;
  tx_delay = 0;

  while ((i = getopt (argc, argv, "hd:p:x:")) != -1)
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
	  peers[n].addr.sin_port = atoi(c);
	}
      else 
	peers[n].addr.sin_port = FLOWPORT;

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
      peers[n].addr.sin_addr.s_addr = scan_ip(tmp_buf);

      peers[n].addr.sin_family = AF_INET;

      if ((peers[n].fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	{
	  fprintf(stderr, "socket(): %s\n", strerror(errno));
	  exit (1);
	}
    }

  /* setup to receive flows */
  bzero(&local_address, sizeof local_address);
  local_address.sin_family = AF_INET;
  local_address.sin_addr.s_addr = htonl(INADDR_ANY);
  local_address.sin_port = htons(fport);

  if ((fsockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    fprintf(stderr, "socket(): %s\n", strerror(errno));
    exit(1);
  }
  if (bind(fsockfd, (struct sockaddr*)&local_address, sizeof local_address) < 0) {
    fprintf(stderr, "bind(): %s\n", strerror(errno));
    exit (1);
  }

  while (1)
    {
      len = sizeof remote_address;
      if ((n = recvfrom(fsockfd, (char*)fpdu,
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
	      if (sendto (peers[i].fd, (char*)fpdu, n, 0,
			  (struct sockaddr*)&peers[i].addr,
			  sizeof (struct sockaddr_in))
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
