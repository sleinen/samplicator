/*
 samplicator.h

 Date Created: Tue Feb 23 19:00:32 2010
 Author:       Simon Leinen  <simon.leinen@switch.ch>
 */

#ifndef _SAMPLICATOR_H_
#define _SAMPLICATOR_H_

enum peer_flags
{
  pf_SPOOF	= 0x0001,
  pf_CHECKSUM	= 0x0002,
};

struct samplicator_context {
  struct source_context *sources;
  struct in_addr	faddr;
  int			fport;
  long			sockbuflen;
  int			debug;
  int			fork;
  enum peer_flags	defaultflags;

  int			fsockfd;
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

struct source_context {
  struct source_context *next;
  struct in_addr	source;
  struct in_addr	mask;
  struct peer	      *	peers;
  unsigned		npeers;
  unsigned		tx_delay;
  int			debug;
};

#endif /* not _SAMPLICATOR_H_ */
