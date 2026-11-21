/*
 * port.h - What ed.c took from the V7 headers and libc. Its K&R calls find
 * read(), write() and the others in port.c, implicitly declared.
 */
#ifndef PORT_H
#define PORT_H

#define SIGHUP 1
#define SIGINT 2
#define SIGQUIT 3
#define SIGTERM 15
#define SIG_DFL ((int (*)())0)
#define SIG_IGN ((int (*)())1)
int (*signal())();

#endif
