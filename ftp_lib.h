#ifndef FTP_LIB
#define FTP_LIB

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <limits.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#define SERVER_CONTROL_PORT 21
#define SERVER_DATA_PORT 20

#define LINELEN 1024

extern const char *ABOR;
extern const char *LIST;
extern const char *PASS;
extern const char *PORT;
extern const char *QUIT;
extern const char *RETR;
extern const char *STOR;
extern const char *SYST;
extern const char *TYPE;
extern const char *USER;
extern const char *UNKNOW;

enum FTP_CMD{CMD_ABOR, CMD_LIST, CMD_PASS, CMD_PORT, CMD_QUIT,
	     CMD_RETR, CMD_STOR, CMD_SYST, CMD_TYPE, CMD_USER,
	     CMD_UNKNOW,};
enum FTP_RESPONSE_CODE{PORT_CODE,};

extern int abort_flag;
void sig_abort(int sig);
int copybetween2fd(int destfd, int srcfd);
 
int connectserveraddr(struct sockaddr * peeraddr, socklen_t peerlen,
		      struct sockaddr *localaddr, socklen_t locallen);
int connectserver(in_addr_t peeraddr, in_port_t peerport,
		  in_addr_t localaddr, in_port_t localport);

int initserver(int type, const struct sockaddr *addr, socklen_t len, int qlen);
const char *cmd2str(enum FTP_CMD cmd);

int readline(int sockfd, char *linebuf, int linelen, 
	     int *lineused, char **linerecvd);

int pharsecmdline(char *line, enum FTP_CMD *cmdp, char **paramsp);

#endif //FTP_LIB
