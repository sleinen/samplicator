/*
 rawsend.h

 Date Created: Wed Jan 19 18:53:42 2000
 Author:       Simon Leinen  <simon@limmat.switch.ch>
 */

extern int raw_send_from_to (int,
			     const void *, size_t,
			     struct sockaddr_in *, size_t,
			     struct sockaddr_in *, size_t);

#define MAX_IP_DATAGRAM_SIZE 1500
