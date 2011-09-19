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

#include "samplicator.h"
#include "read_config.h"
#include "rawsend.h"

#define PDU_SIZE 1500

static int send_pdu_to_receiver (struct receiver *, const void *, size_t,
				 struct sockaddr_in *);
static int init_samplicator (struct samplicator_context *);
static int samplicate (struct samplicator_context *);

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
     const char **argv;
{
  struct samplicator_context ctx;
  struct source_context cmd_line;

  cmd_line.source.s_addr = 0;
  cmd_line.mask.s_addr = 0;
  cmd_line.nreceivers = 0;
  ctx.sources = &cmd_line;

  cmd_line.next = (struct source_context *) NULL;

  parse_args (argc, argv, &ctx, &cmd_line);

  if (init_samplicator (&ctx) == -1)
    exit (1);
  if (samplicate (&ctx) != 0) /* actually, samplicate() should never return. */
    exit (1);
  exit (0);
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
  local_address.sin_addr.s_addr = ctx->faddr.s_addr;
  local_address.sin_port = htons (ctx->fport);

  if ((ctx->fsockfd = socket (AF_INET, SOCK_DGRAM, 0)) < 0)
    {
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
	    (struct sockaddr*)&local_address, sizeof local_address) < 0)
    {
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
  pid_t pid;
  int i, n;
  socklen_t len;

  /* check is there actually at least one configured data receiver */
  for (i = 0, sctx = ctx->sources; sctx != NULL; sctx = sctx->next)
    if(sctx->nreceivers > 0)  i += sctx->nreceivers; 
  if (i == 0)
    {
      fprintf(stderr, "You have to specify at least one receiver, exiting\n");
      exit(1);
    }

  if (ctx->fork == 1)
    {
      pid = fork();
      if (pid == -1)
        {
          fprintf (stderr, "failed to fork process\n");
          exit (1);
        }
      else if (pid > 0)
        { /* kill the parent */
          exit (0);
        }
      else
        { /* end interaction with shell */
          fclose(stdin);
          fclose(stdout);
          fclose(stderr);
        }
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
	  fprintf (stderr, "recvfrom() return address length %lu - expected %lu\n",
		   (unsigned long) len, (unsigned long) sizeof remote_address);
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
	  if ((sctx->source.s_addr == 0)
	      || ((remote_address.sin_addr.s_addr & sctx->mask.s_addr)
		  == sctx->source.s_addr))
	    for (i = 0; i < sctx->nreceivers; ++i)
	      {
		if (sctx->receivers[i].freqcount == 0)
		  {
		    if (send_pdu_to_receiver (& (sctx->receivers[i]), fpdu, n, &remote_address)
			== -1)
		      {
			fprintf (stderr, "sending datagram to %s:%d failed: %s\n",
				 inet_ntoa (sctx->receivers[i].addr.sin_addr),
				 (int) ntohs (sctx->receivers[i].addr.sin_port),
				 strerror (errno));
		      }
		    else if (ctx->debug)
		      {
			fprintf (stderr, "  sent to %s:%d\n",
				 inet_ntoa (sctx->receivers[i].addr.sin_addr),
				 (int) ntohs (sctx->receivers[i].addr.sin_port)); 
		      }
		    sctx->receivers[i].freqcount = sctx->receivers[i].freq-1;
		  }
		else
		  {
		    --sctx->receivers[i].freqcount;
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

static int
send_pdu_to_receiver (receiver, fpdu, length, source_addr)
     struct receiver * receiver;
     const void * fpdu;
     size_t length;
     struct sockaddr_in * source_addr;
{
  if (receiver->flags & pf_SPOOF)
    {
      int rawsend_flags
	= ((receiver->flags & pf_CHECKSUM) ? RAWSEND_COMPUTE_UDP_CHECKSUM : 0);
      return raw_send_from_to (receiver->fd, fpdu, length,
			       (struct sockaddr *) source_addr,
			       (struct sockaddr *) &receiver->addr,
			       receiver->ttl, rawsend_flags);
    }
  else
    {
      return sendto (receiver->fd, (char*) fpdu, length, 0,
		     (struct sockaddr*) &receiver->addr,
		     sizeof (struct sockaddr_in));
    }
}
