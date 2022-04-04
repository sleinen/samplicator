Instructions for installing the samplicator
===========================================

Copyright (c) 2000-2015 Simon Leinen  <simon.leinen@gmail.com>

Creating the configure script (when installing from Git)
--------------------------------------------------------

The configure script is not included in the source repository.  You
can create it using `autogen.sh`.  Note that GNU automake and GNU
autoconf are required for this.

Configure, Compile, and Install
-------------------------------

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

Startup script for systemd
--------------------------

A simple `samplicator.service` systemd Service File for samplicator is
included. It works at least on CentOS 7.x, use as an example:

- modify `ExecStart` as desired for your local situation
- write the referred `samplicator.conf`

Then install and start the new service. On my CentOS 7.2, it looks like this:

	cp samplicator.service /etc/systemd/system/samplicator.service
	systemctl daemon-reload
	systemctl start samplicator.service
	
Run Samplicator in Docker
--------------------------
Example:
```bash
docker run -td sleinen/samplicator \
	-e samplicator_port=1700 \
	-e samplicator_arguments='192.168.1.2/1700 192.168.1.16/1700 192.168.2.23/1700'
```
