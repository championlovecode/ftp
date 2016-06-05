#include "ftp_lib.h"

const char *ABOR = "ABOR";
const char *LIST = "LIST"; /* list file or directory */
const char *PASS = "PASS";
const char *PORT = "PORT";
const char *QUIT = "QUIT";
const char *RETR = "RETR"; /* RETRIEVE */
const char *STOR = "STOR"; /* STORE */
const char *SYST = "SYST";
const char *TYPE = "TYPE";
const char *USER = "USER";
const char *UNKNOW = "UNKNOWN";

int abort_flag;
void sig_abort(int sig)
{
  abort_flag = 1;
}
#define BUFLEN 8192
int copybetween2fd(int destfd, int srcfd)
{
  int n, nr, nw, totaln;
  char buf[BUFLEN];
  totaln = 0;
  abort_flag = 0;
  signal(SIGUSR1, sig_abort);
  while (!abort_flag && (nr = read(srcfd, buf, BUFLEN)) > 0) {
    int n = 0;
    while ((nw = write(destfd, buf+n, nr-n)) > 0 && n < nr) {
      n += nw;
    }
    totaln += nr;
  }
  signal(SIGUSR1, SIG_DFL);
  return totaln;
}

int initserver(int type, const struct sockaddr *addr,
	       socklen_t addrlen, int qlen)
{
  int servfd, err, val;
  servfd = socket(addr->sa_family, type, 0);

  if (servfd == -1)
    return -1;
  int reuse = 1;
  if (setsockopt(servfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0)
    goto errout;
  if (bind(servfd, addr, addrlen) < 0)
    goto errout;
  if (type == SOCK_STREAM || type == SOCK_SEQPACKET) {
    if (listen(servfd, qlen) < 0)
      goto errout;
  }
  return servfd;
 errout:
  err = errno;
  close(servfd);
  errno = err;
  return -1;
}
int connectserveraddr(struct sockaddr * peeraddr, socklen_t peerlen,
		      struct sockaddr *localaddr, socklen_t locallen)
{
  int sockfd, val, err, reuse;
  sockfd = socket(AF_INET, SOCK_STREAM, 0);
  reuse = 1;
  if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0)
    goto errout;

  if (localaddr) {
    if (bind(sockfd, localaddr, locallen)<0)
      goto errout;
  }
  if (connect(sockfd, peeraddr, peerlen)<0)
    goto errout;

  //  val = fcntl(sockfd,F_GETFL, 0);
  //  fcntl(sockfd, F_SETFL, val | O_NONBLOCK);
  return sockfd;
 errout:
  err = errno;
  close(sockfd);
  errno = err;
  return -1;
}
int connectserver(in_addr_t peeraddr, in_port_t peerport,
		  in_addr_t localaddr, in_port_t localport)
{
  struct sockaddr_in sa, salocal;
  sa.sin_family = AF_INET;
  sa.sin_addr.s_addr = peeraddr;
  sa.sin_port = peerport;

  salocal.sin_family = AF_INET;
  salocal.sin_addr.s_addr = localaddr;
  salocal.sin_port = localport;
  return connectserveraddr((struct sockaddr*) &sa, sizeof(sa),
			   (struct sockaddr*) &salocal, sizeof(salocal));
}

const char *cmd2str(enum FTP_CMD cmd)
{
  switch(cmd) {
  case CMD_ABOR: return ABOR;
  case CMD_LIST: return LIST;
  case CMD_PASS: return PASS;
  case CMD_PORT: return PORT;
  case CMD_QUIT: return QUIT;
  case CMD_RETR: return RETR;
  case CMD_STOR: return STOR;
  case CMD_SYST: return SYST;
  case CMD_TYPE: return TYPE;
  case CMD_USER: return USER;
  case CMD_UNKNOW: return UNKNOW;
  default: return "";
  }
}

int pharseserverresponse(const char *line)
{
  return 0;
}

/* read socket until reach line end symbole \n */
int readline(int sockfd, char *linebuf, int linelen, 
	     int *lineusedp, char **linerecvd)
{
  char *buf, *endline;
  int n, ntotal, bufsize, lineused;
  int found;
  
  *linerecvd = NULL;

  found = 0;
  ntotal = 1;
  lineused = *lineusedp;

  linebuf[lineused] = '\0';
  endline = strchr(linebuf, '\n');
  if (endline != NULL)
    found = 1;
  
  while (!found) {
    buf = linebuf + lineused;
    bufsize = linelen - lineused;  
    
    n = recv(sockfd, buf, bufsize-1, 0);

    if ( n <= 0)
      return n;
    ntotal += n;
    lineused += n;
    linebuf[lineused] = '\0';

    endline = strchr(buf, '\n');
    if (endline != NULL) {
      found = 1;
    } else if (lineused == linelen - 1) {
      fprintf(stderr, "command line exceed: %s\n", linebuf);
      lineused = 0;
    }
  }

  *endline = '\0'; // replace \n to \0, cut line into 2 string
  endline ++;
  *linerecvd = strdup(linebuf);
  if (*linerecvd == NULL) { /* dump line */
    fprintf(stderr, "FETAL ERROR: insufficient memory on strdup\n");
    exit(EXIT_FAILURE);
  }
  lineused -= strlen(linebuf) + 1;
  linebuf[lineused] = '\0';
  strncpy(linebuf, endline, lineused);	/* copy left */
  *lineusedp = lineused;
  return ntotal;
}

/* return 0 if success, -1 for wrong command */
int pharsecmdline(char *line, enum FTP_CMD *cmdp, char **paramsp)
{
  char *cmd, *params;
  const char* delims = " \r\n";

  if ((line = strdup(line)) == NULL) {
    fprintf(stderr, "FETAL ERROR: insufficient memory on strdup\n");
    exit(EXIT_FAILURE);
  }
  cmd = strtok(line, delims);
  params = strtok(NULL, "\r\n");

  cmd = strdup(cmd);
  if (params) {
    if ((params = strdup(params)) == NULL)  {
      fprintf(stderr, "FETAL ERROR: insufficient memory on strdup\n");
      exit(EXIT_FAILURE);
    }
  }
    
  if(strcasecmp(cmd, ABOR) == 0) {
    *cmdp = CMD_ABOR;
  } else if (strcasecmp(cmd, LIST) == 0) { 
    *cmdp = CMD_LIST;
  } else if(strcasecmp(cmd, PASS) == 0) {
    *cmdp = CMD_PASS;
  } else if (strcasecmp(cmd, PORT) == 0) {
    *cmdp = CMD_PORT;
  } else if (strcasecmp(cmd, RETR) == 0) { 
    *cmdp = CMD_RETR;
  } else if(strcasecmp(cmd, STOR) == 0) {
    *cmdp = CMD_STOR;
  } else if(strcasecmp(cmd, SYST) == 0) {
    *cmdp = CMD_SYST;
  } else if(strcasecmp(cmd, TYPE) == 0) {
    *cmdp = CMD_TYPE;
  } else if(strcasecmp(cmd, USER) == 0) {
    *cmdp = CMD_USER;
  } else {
    *cmdp = CMD_UNKNOW;
    goto errout;
  }
 okout:
  if(paramsp)
    *paramsp = params;
  free(cmd);
  return 0;  
 errout:
  if (paramsp)
    *paramsp = NULL;
  free(cmd);
  if(params)
    free(params);
  return -1;
}
