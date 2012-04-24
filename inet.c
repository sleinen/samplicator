/*
 inet.c

 Date Created: Wed Feb 24 17:48:08 2010
 Author:       Simon Leinen  <simon.leinen@switch.ch>
 */

#include "config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#include <string.h>
#include <errno.h>
#if STDC_HEADERS
# define bzero(b,n) memset(b,0,n)
#else
# include <strings.h>
#endif

#include "samplicator.h"
#include "inet.h"

void
init_hints_from_preferences (hints, ctx)
     struct addrinfo *hints;
     const struct samplicator_context *ctx;
{
  bzero (hints, sizeof *hints);
  hints->ai_flags = AI_PASSIVE|AI_ADDRCONFIG;
  hints->ai_socktype = SOCK_DGRAM;
  if (ctx->ipv4_only)
    {
      hints->ai_family = AF_INET;
    }
  else if (ctx->ipv6_only)
    {
      hints->ai_family = AF_INET6;
    }
  else
    {
      hints->ai_family = AF_UNSPEC;
    }
}
