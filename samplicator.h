/*
 samplicator.h

 Date Created: Tue Feb 23 19:00:32 2010
 Author:       Simon Leinen  <simon.leinen@switch.ch>
 */

#ifndef _SAMPLICATOR_H_
#define _SAMPLICATOR_H_

enum receiver_flags
{
  pf_SPOOF	= 0x0001,
  pf_CHECKSUM	= 0x0002,
  pf_SPOOF_WITH_IP	= 0x0004,
  
};

struct samplicator_context {
  struct source_context        *sources;
  const char		       *faddr_spec;
  struct sockaddr_storage	faddr;
  const char		       *fport_spec;
  long				sockbuflen;
  long				pdulen;
  int				debug;
  int				timeout;
  int				fork;
  int				ipv4_only;
  int				ipv6_only;
  const char		       *pid_file;
  enum receiver_flags		default_receiver_flags;

  int				fsockfd;
  socklen_t			fsockaddrlen;

  const char		       *config_file_name;
  int				config_file_lineno;
  
  const char *spoofed_src_addr;

  /* statistics */
  uint32_t			unmatched_packets;
};

struct receiver {
  int				fd;
  struct sockaddr_storage	addr;
  socklen_t			addrlen;
  int				port;
  int				freq;
  int				freqcount;
  int				ttl;
  enum receiver_flags		flags;
  
  struct sockaddr_storage spoofed_src_addr;
  socklen_t spoofed_src_addrlen;
  

  /* statistics */
  uint32_t			out_packets;
  uint32_t			out_errors;
  uint64_t			out_octets;
};

struct source_context {
  struct source_context	       *next;
  struct sockaddr_storage	source;
  struct sockaddr_storage	mask;
  socklen_t			addrlen;
  struct receiver	       *receivers;
  unsigned			nreceivers;
  unsigned			tx_delay;
  int				debug;

  /* statistics */
  uint32_t			matched_packets;
  uint64_t			matched_octets;
};

#endif /* not _SAMPLICATOR_H_ */
