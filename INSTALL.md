Instructions for installing the samplicator
===========================================

Copyright (c) 2000-2015 Simon Leinen  <simon.leinen@gmail.com>

This distribution uses GNU configure for portability and flexibility
of installation.  You must configure the program for your system
before you can compile it using make:

	$ ./configure
	$ make

The program can then be installed (under /usr/local/bin by default)
using "make install":

	$ make install

The configure script accepts many arguments, some of which may even be
useful.  Using "./configure --prefix ..." you can specify a directory
other than /usr/local to be used as an installation destination.  Call
"./configure --help" to get a list of arguments accepted by configure.
