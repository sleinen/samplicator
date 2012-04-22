/*
 rawsend.h

 Date Created: Wed Jan 19 18:53:42 2000
 Author:       Simon Leinen  <simon.leinen@switch.ch>
 */

#define DEFAULT_TTL 64

#define RAWSEND_COMPUTE_UDP_CHECKSUM	0x0001

extern int make_raw_udp_socket (int);
extern int raw_send_from_to (int,
			     const void *, size_t,
			     struct sockaddr_in *,
			     struct sockaddr_in *,
			     int,
			     int);
