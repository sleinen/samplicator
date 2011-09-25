/*
 parsetest.c

 Date Created: Tue Feb 23 20:43:59 2010
 Author:       Simon Leinen  <simon.leinen@switch.ch>

 Regression tests for samplicator's configuration file parsing.

 I wanted to add functionality to samplicator's configuration file
 parsing.  For example, it should be able to handle IPv6 addresses in
 addition to IPv4 addresses.

 In order to make sure I don't break the existing functionality -
 which someone else had written and which I didn't fully understand -
 I decided to write exhaustive tests, at least for the cases where
 files are parsed successfully.  And I also wrote some tests for the
 new functionality.

 If all goes well, this program outputs a series of "ok" lines.  If
 any tests fail, the program will print a "fail" message.  The ok/fail
 messages only contain running numbers, so it's hard to find out what
 exactly went wrong.  The way I use this is that I run this test
 program under a debugger such as GDB, with a breakpoint set at
 test_fail().  When the breakpoint is hit, I move up the stack to see
 what has gone wrong.
 */

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
#include <netdb.h>
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

#include "samplicator.h"
#include "read_config.h"
#include "rawsend.h"

static int parse_cf_string (const char *, struct samplicator_context *);
static int check_int_equal (int, int);
static int check_non_null (const void *);
static int check_null (const void *);
static int check_address_equal (struct sockaddr *, const char *, unsigned, int);
static int test_ok (void);
static int test_fail (void);
static int test_index = 1;

static void
check_receiver (receiver, addr, port, af, freq, ttl)
     const struct receiver *receiver;
     const char *addr;
     unsigned port;
     int af;
     int freq;
     int ttl;
{
  check_int_equal (receiver->addr.ss_family, af);
  if (af == AF_INET)
    check_int_equal (receiver->addrlen, sizeof (struct sockaddr_in));
  else if (af == AF_INET6)
    check_int_equal (receiver->addrlen, sizeof (struct sockaddr_in6));
  else
    test_fail ();
  check_address_equal ((struct sockaddr *) &receiver->addr, addr, port, af);
  check_int_equal (receiver->freq, freq);
  check_int_equal (receiver->ttl, ttl);
}

static void
check_source_address_mask (sctx, source, mask, af)
     const struct source_context *sctx;
     const char *source, *mask;
     int af;
{
  check_address_equal ((struct sockaddr *) &sctx->source, source, 0, af);
  check_address_equal ((struct sockaddr *) &sctx->mask, mask, 0, af);
}

int
main (int argc, char **argv)
{
  struct samplicator_context ctx;
  struct source_context *sctx;

  if (argc != 1)
    {
      fprintf (stderr, "Usage: %s\n", argv[0]);
      exit (1);
    }
  check_int_equal (parse_cf_string ("", &ctx), 0);
  check_int_equal (ctx.fork, 0);
  check_null (ctx.sources);

  check_int_equal (parse_cf_string ("1.2.3.4/30+ 6.7.8.9/1234\n", &ctx), -1);

  check_int_equal (parse_cf_string ("1.2.3.4/255.255.255.252: 6.7.8.9/1234/10,237\n2.3.4.5: 7.8.9.0/4321", &ctx), 0);
  check_int_equal (ctx.fork, 0);
  if (check_non_null (sctx = ctx.sources))
    {
      check_source_address_mask (sctx, "1.2.3.4", "255.255.255.252", AF_INET);
      check_int_equal (sctx->nreceivers, 1);
      check_receiver (&sctx->receivers[0], "6.7.8.9", 1234, AF_INET, 10, 237);
      if (check_non_null (sctx = sctx->next))
	{
	  check_source_address_mask (sctx, "2.3.4.5", "255.255.255.255", AF_INET);
	  check_int_equal (sctx->nreceivers, 1);
	  check_receiver (&sctx->receivers[0], "7.8.9.0", 4321, AF_INET, 1, DEFAULT_TTL);
	  check_null (sctx->next);
	}
    }

  check_int_equal (parse_cf_string ("1.2.3.4/30: 6.7.8.9/1234\n2.3.4.5: 7.8.9.0/4321", &ctx), 0);
  check_int_equal (ctx.fork, 0);
  if (check_non_null (sctx = ctx.sources))
    {
      check_source_address_mask (sctx, "1.2.3.4", "255.255.255.252", AF_INET);
      check_int_equal (sctx->nreceivers, 1);
      check_receiver (&sctx->receivers[0], "6.7.8.9", 1234, AF_INET, 1, DEFAULT_TTL);
      if (check_non_null (sctx = sctx->next))
	{
	  check_source_address_mask (sctx, "2.3.4.5", "255.255.255.255", AF_INET);
	  check_int_equal (sctx->nreceivers, 1);
	  check_receiver (&sctx->receivers[0], "7.8.9.0", 4321, AF_INET, 1, DEFAULT_TTL);
	  check_null (sctx->next);
	}
    }

#ifdef NOTYET
  check_int_equal (parse_cf_string ("1.2.3.4/30: localhost/1234", &ctx), 0);
  check_int_equal (ctx.fork, 0);
  if (check_non_null (sctx = ctx.sources))
    {
      check_source_address_mask (sctx, "1.2.3.4", "255.255.255.252", AF_INET);
      check_int_equal (sctx->nreceivers, 1);
      check_receiver (&sctx->receivers[0], "127.0.0.1", 1234, AF_INET, 1, DEFAULT_TTL);
    }
#endif

  check_int_equal (parse_cf_string ("1.2.3.4/30: ip6-localhost/1234", &ctx), 0);
  check_int_equal (ctx.fork, 0);
  if (check_non_null (sctx = ctx.sources))
    {
      check_source_address_mask (sctx, "1.2.3.4", "255.255.255.252", AF_INET);
      check_int_equal (sctx->nreceivers, 1);
      check_receiver (&sctx->receivers[0], "::1", 1234, AF_INET6, 1, DEFAULT_TTL);
    }

  check_int_equal (parse_cf_string ("[0::0]/0: [2001:db8:0::1]/1234/10,34\n", &ctx), 0);
  check_int_equal (ctx.fork, 0);
  sctx = ctx.sources;
  if (check_non_null (sctx))
    {
      check_source_address_mask (sctx, "::", "::", AF_INET6);
      check_int_equal (sctx->nreceivers, 1);
      check_receiver (&sctx->receivers[0], "2001:db8:0::1", 1234, AF_INET6, 10, 34);
      check_null (sctx->next);
    }

  check_int_equal (parse_cf_string ("[0::0]/0: [2001:db8:0::1]/1234/10,34\n", &ctx), 0);
  check_int_equal (ctx.fork, 0);
  sctx = ctx.sources;
  if (check_non_null (sctx))
    {
      check_source_address_mask (sctx, "::", "::", AF_INET6);
      check_int_equal (sctx->nreceivers, 1);
      check_receiver (&sctx->receivers[0], "2001:db8:0::1", 1234, AF_INET6, 10, 34);
      check_null (sctx->next);
    }

  check_int_equal (parse_cf_string ("[2001:db8:0:4::]: [2001:db8:0::1]\n", &ctx), 0);
  check_int_equal (ctx.fork, 0);
  sctx = ctx.sources;
  if (check_non_null (sctx))
    {
      check_source_address_mask
	(sctx, "2001:db8:0:4::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", AF_INET6);
      check_int_equal (sctx->nreceivers, 1);
      check_receiver (&sctx->receivers[0], "2001:db8:0::1", 2000, AF_INET6, 1, DEFAULT_TTL);
      check_null (sctx->next);
    }
#ifdef NOTYET
#endif
  return 0;
}

static int
parse_cf_string (s, ctx)
     const char *s;
     struct samplicator_context *ctx;
{
  const char *test_file_name = "parsetest.cf";
  FILE *fp;
  const char *args[20];
  const char **ap;
  int n_args;

  unlink (test_file_name);
  fp = fopen (test_file_name, "w");
  if (fp == (FILE *) 0)
    {
      fprintf (stderr, "Could not create test file %s: %s",
	       test_file_name,
	       strerror (errno));
      return -1;
    }
  if (fputs (s, fp) == EOF)
    {
      fprintf (stderr, "Error writing to test file %s: %s",
	       test_file_name,
	       strerror (errno));
      return -1;
    }
  if (fclose (fp) != 0)
    {
      fprintf (stderr, "Error closing test file %s: %s",
	       test_file_name,
	       strerror (errno));
      return -1;
    }
  ap = &args[0];
  *ap++ = "parsetest";
  *ap++ = "-c";
  *ap++ = test_file_name;
  n_args = ap-args;
  *ap++ = (char *) 0;
  return parse_args (n_args, args, ctx);
}

static int
check_int_equal (is, should)
     int is;
     int should;
{
  if (is == should)
    {
      return test_ok ();
    }
  else
    {
      return test_fail ();
    }
}

static int
check_non_null (ptr)
     const void *ptr;
{
  return  check_int_equal (ptr == 0, 0);
}

static int
check_null (ptr)
     const void *ptr;
{
  return  check_int_equal (ptr == 0, 1);
}

static int
check_sockaddrs_equal (sa1_generic, sa2_generic)
     struct sockaddr *sa1_generic;
     struct sockaddr *sa2_generic;
{
#define SPECIALIZE(VAR, STRUCT) \
  struct STRUCT *VAR = (struct STRUCT *) VAR ## _generic
  if (sa1_generic->sa_family != sa2_generic->sa_family)
    return test_fail ();
  if (sa1_generic->sa_family == AF_INET)
    {
      SPECIALIZE(sa1, sockaddr_in);
      SPECIALIZE(sa2, sockaddr_in);
      check_int_equal (sa1->sin_port, sa2->sin_port);
      if (memcmp (&sa1->sin_addr, &sa2->sin_addr, sizeof (struct in_addr)) != 0)
	return test_fail ();
      return test_ok ();
    }
  else if (sa1_generic->sa_family == AF_INET6)
    {
      SPECIALIZE(sa1, sockaddr_in6);
      SPECIALIZE(sa2, sockaddr_in6);
      check_int_equal (sa1->sin6_port, sa2->sin6_port);
      if (memcmp (&sa1->sin6_addr, &sa2->sin6_addr, sizeof (struct in6_addr)) != 0)
	return test_fail ();
      return test_ok ();
    }
  else
    return test_fail ();
#undef SPECIALIZE
}

static int
check_address_equal (sa1, s2, port, af)
     struct sockaddr *sa1;
     const char *s2;
     unsigned port;
     int af;
{
  struct addrinfo hints, *res;
  int result;
  char portspec[10];

  sprintf (portspec, "%5.5d", port);
  bzero (&hints, sizeof hints);
  hints.ai_family = af;

  result = getaddrinfo (s2, portspec, &hints, &res);
  if (result != 0)
    return test_fail ();
  if (res->ai_family != af)
    return test_fail ();
  return check_sockaddrs_equal (sa1, res->ai_addr);
}

static int
test_ok ()
{
  fprintf (stdout, "%3d... ok\n", test_index++);
  return 1;
}

static int
test_fail ()
{
  fprintf (stdout, "%3d... fail\n", test_index++);
  return 0;
}
