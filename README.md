Bouncer
=========

Description
-----------
When leaving X and returning to the console, most window managers will forcefully destroy all
windows still open, giving you no chance to gracefully close some applications.

Bouncer sends _ NET_CLOSE_WINDOW messages to every window matching a pattern and
blocks until all those windows are closed or a specified timeout expires.


Dependencies
------------

- GNU C Compiler
- GNU Make
- libxcb


Installing
----------

Simply running

	make install
as root will install the binaries to */usr/bin*, configuration to */etc* and MAN-pages to */usr/share/man/*.
If you want to install to a fake root directory (e.g. for package creation) use the *prefix*-variable.

	make install prefix=/fake/root
The subdirectories *usr/bin*, *usr/share/man* and *etc* must exist.

Debugging
---------

	make debug
Compiles source files without optimizations and with debugging informations (-O0 -g).


Usage
-----
Instead of just calling

	$ shutdown -h
you could call

	$ bouncer -t 30 -p "Firefox" && shutdown -h

This will send a _NET_CLOSE_WINDOW message to every window with the string "Firefox" in its
WM_CLASS property and wait until they are closed or 30 seconds elapsed.

As root you can call

	$ bouncer-global -t 30 -p "Firefox" && shutdown -h
to affect all Displays.

The file *~/.bouncerc* can be used as alternative to the -p argument.


Service file
------------

Bouncer comes with the systemd service file *bouncer@.service*.
Run

	$ systemctl enable bouncer@60

to run

	$ bouncer-global -t 60

at each shutdown.

