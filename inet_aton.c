/*
 inet_aton.c

 Date Created: Thu Jan 20 20:46:16 2000
 Author:       Simon Leinen  <simon.leinen@switch.ch>

 This file implements the function inet_aton(), which converts a
 string in dotted-quad notation to an IPv4 address (struct in_addr).
 It should be able to cope with other historic formats with zero, one,
 two or three dots too, although I'm not sure I got them all right.
 */

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
/* #define TEST 1 */
#ifdef TEST
#include <stdio.h>
#endif

static int inet_aton_1 (const char *, const char *, struct in_addr *);

extern int
inet_aton (const char *s, struct in_addr *out)
{
  return inet_aton_1 (s, s+strlen (s), out);
}

static int
inet_aton_1 (const char *start, const char *end, struct in_addr *out)
{
  long addr = 0;
  unsigned long component[4], n;
  unsigned ncomp, k;
  const char *s;

  for (s = start, ncomp = 0; s < end; ++s, component[ncomp++] = n)
    {
      for (n = 0; s < end && *s != '.'; ++s)
	{
	  if (*s < '0' || *s > '9')
	    return 0;
	  n = n * 10 + (*s - '0');
	}
    }
  if (ncomp == 0 || ncomp > 4)
    return 0;
  else
    {
      switch (ncomp)
	{
	case 1:
	  addr = component[0];
	  break;
	case 2:
	  if (component[0] > 255 || component[1] > 16777215)
	    return 0;
	  addr = component[0] << 24 | component[1];
	  break;
	case 3:
	  if (component[0] > 255 || component[1] > 255 || component[2] > 65535)
	      return 0;
	  addr = component[0] << 24 | component[1] << 16 | component[2];
	  break;
	case 4:
	  for (k = 0; k < 4; ++k)
	    {
	      if (component[k] > 255)
		  return 0;
	      addr <<= 8;
	      addr |= component[k];
	    }
	  break;
	default:
	  return 0;
	}
      out->s_addr = htonl (addr);
      return 1;
    }
}

#ifdef TEST
extern int
main (argc, argv)
     int argc;
     char **argv;
{
  char buf[256];
  int buflen = 256;
  struct sockaddr_in addr;

  while (1)
    {
      fgets (buf, buflen, stdin);
      if (buf[strlen (buf)-1] == 10)
	buf[strlen (buf)-1] = 0;
      if (inet_aton (buf, &addr.sin_addr) == 0)
	{
	  fprintf (stderr, "Conversion failed.\n");
	}
      else
	{
	  printf ("-> %08lx [%s]\n",
		  (unsigned long) addr.sin_addr.s_addr,
		  inet_ntoa (addr.sin_addr));
	}
    }
  return 0;
}
#endif /* TEST */
