## Process this file with automake to produce Makefile.in

PACKAGE = samplicator

bin_PROGRAMS = samplicate
samplicate_SOURCES = samplicate.c samplicator.h rawsend.c rawsend.h read_config.c read_config.h
samplicate_LDADD = @LIBOBJS@

EXTRA_PROGRAMS = rawtest parsetest
rawtest_SOURCES = rawtest.c rawsend.c rawsend.h
parsetest_SOURCES = parsetest.c read_config.c rawsend.c read_config.h rawsend.h samplicator.h
