/*
 read_config.h

 Date Created: Tue Feb 23 18:59:54 2010
 Author:       Simon Leinen  <simon.leinen@switch.ch>
 */

extern int read_cf_file (const char *, struct samplicator_context *);
extern int parse_receivers (int, const char **, struct samplicator_context *, struct source_context *);
extern int parse_args (int, const char **, struct samplicator_context *, struct source_context *);
