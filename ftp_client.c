#include "ftp_client.h"
static enum FTP_CMD lastcmd = CMD_UNKNOW;
static char *lastparams = NULL;
static int cntl_sockfd = -1;
static int data_serv_sockfd = -1;
static int data_sockfd = -1;
static const char *peername = NULL;
static char localname[HOST_NAME_MAX+1];
static char username[LOGIN_NAME_MAX+1];
static int logedon = 0;
static in_addr_t peeraddr;
static in_addr_t localaddr;
static in_addr_t curdataaddr;
static in_port_t curdataport;
static int datapid = -1;
static char cntlinbuf[LINELEN+1];
static char cntloutbuf[LINELEN+1];
static int cntlinbufused = 0;
static char stdinbuf[LINELEN+1];
static char homedir[PATH_MAX+1], ftpdir[PATH_MAX];

int init_data_server()
{
  int fd, retval, err, alen;
  struct sockaddr_in sa;
  sa.sin_family = AF_INET;
  sa.sin_port = 0;
  //sa.sin_addr.s_addr = 0;
  sa.sin_addr.s_addr = htonl (INADDR_ANY); /* wildcard */

  alen = sizeof(sa);
  if ((fd = initserver(SOCK_STREAM,
		       (struct sockaddr*)&sa, sizeof(sa), 1)) < 0) {
    fprintf(stderr, "error on init data server: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (getsockname(fd, (struct sockaddr*)&sa, &alen) < 0) {
    fprintf(stderr, "error on init data server: %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  curdataport = sa.sin_port;
  curdataaddr = sa.sin_addr.s_addr;
  if (curdataaddr == INADDR_ANY) {
    struct addrinfo hints, *alist, *aip;
    int err;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    //if ((err = getaddrinfo(localname,0,&hints, &alist)) != 0) 
	if ((err = getaddrinfo("192.168.1.155",0,&hints, &alist)) != 0) 
	{
      fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(err));
      exit(EXIT_FAILURE);
    }
    for (aip = alist; aip != NULL; aip = aip->ai_next) {
      struct sockaddr_in *sin;
      sin = (struct sockaddr_in*)aip->ai_addr;
      curdataaddr = sin->sin_addr.s_addr;
      if (curdataaddr != INADDR_ANY)
	break;
    }
  }
  return fd;
}
int init_cntl_connection()
{
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int s;
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = 0;

  s = getaddrinfo(peername, "ftp", &hints, &result);
  if(s != 0) {
    fprintf(stderr, "getaddinfo: %s\n", gai_strerror(s));
    exit(EXIT_FAILURE);
  }
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    cntl_sockfd = connectserveraddr(rp->ai_addr, rp->ai_addrlen,
				   NULL, 0);
    if (cntl_sockfd >=0){
      char addr[INET_ADDRSTRLEN];
      struct sockaddr_in * sa_in =  (struct sockaddr_in*)rp->ai_addr;
      in_addr_t in_addr = sa_in->sin_addr.s_addr;
      inet_ntop(AF_INET, &in_addr, addr, INET_ADDRSTRLEN);

      fprintf(stdout, "connected on address %s:%u\n", addr,
	      ntohs(sa_in->sin_port));

      peeraddr = in_addr;
      break;
    }
  }
  if (rp == NULL) {
    fprintf(stderr, "Counld not connect to server : %s\n", strerror(errno));
    exit(EXIT_FAILURE);
  }
  freeaddrinfo(result);
}

int logon()
{
  int n;
  char inputname[LOGIN_NAME_MAX+1];
  char *linerecvd;
  printf("Name (%s):", username);
  fflush(stdout);
  
  strcpy(cntloutbuf,USER);
  n = strlen(USER);
  cntloutbuf[n++] = ' ';

  fgets(inputname, LOGIN_NAME_MAX, stdin);
  
  if (strlen(inputname) == 1) {
    strcpy(&cntloutbuf[n], username);
    n+= strlen(username);    
  } else {
    strcpy(&cntloutbuf[n], inputname);
    n += strlen(inputname);
    printf("inputname: %s\n", inputname);
  }
  cntloutbuf[n++] = '\n';
  send(cntl_sockfd, cntloutbuf, n, 0);
  printf("--> %s", cntloutbuf);
  readline(cntl_sockfd, cntlinbuf, LINELEN,
	   &cntlinbufused, &linerecvd);
  puts(linerecvd);
  return 0;
}
static int passwd()
{
  int n, code;
  char *pass, *linerecvd;

  if (logedon)
    return 0;
  do {
    pass = getpass("passwd:");
    sprintf(cntloutbuf,"%s %s\n", PASS, pass);
    //    memset(passwd, 0, strlen(passwd));
    send(cntl_sockfd, cntloutbuf, strlen(cntloutbuf), 0);
    printf("--> %s", cntloutbuf);
    n = readline(cntl_sockfd, cntlinbuf, LINELEN,
		 &cntlinbufused, &linerecvd);
    if (linerecvd == NULL && n == 0) {
      fprintf(stderr, "server closed\n");
      close(cntl_sockfd);
      if (data_sockfd >=0)
	close(data_sockfd);
      exit(EXIT_SUCCESS);
    } else if (n < 0) {
      return;
    }
    printf("%s\n", linerecvd);
    code = atoi(linerecvd);
    if (code == 230)
      logedon = 1;    
  } while (!logedon);
  return 0;
}
static void transfer_data()
{
  int nr, nw, totaln;
  int localfd;
  char *filename = lastparams;
  char path[LINELEN];
  puts("transfering");

  if (filename == NULL) {
    strcpy(path, homedir);
  } else {
    snprintf(path, LINELEN, "%s%s", homedir, filename);
  }

  switch (lastcmd) {
  case CMD_LIST:
    copybetween2fd(STDOUT_FILENO, data_sockfd);
    break;
  case CMD_RETR:
  	printf("\n CMD_RETR path=%s\n",path);
    if (!filename) 
      break;
    localfd = open(path, O_CREAT|O_WRONLY);
    if (localfd < 0) {
      fprintf(stderr, "error open file %s: %s\n", 
	      path, strerror(errno));
      break;
    }
    totaln = copybetween2fd(localfd, data_sockfd);
    if (abort_flag) {
      printf("aborted on retrive file %s\n", path);
    } else {
      printf("retrive file %s complete, %f KB received.\n",
	     path, totaln/1024.0);
    }
    close(localfd);
    break;
  case CMD_STOR:
    if (!filename) 
      break;
    localfd = open(path, O_RDONLY);
    if (localfd < 0) {
      fprintf(stderr, "error open file %s: %s\n", 
	      path, strerror(errno));
      break;
    }
    totaln = copybetween2fd(data_sockfd, localfd);
    if (abort_flag) {
      printf("aborted on store file %s\n", path);
    } else {
      printf("store file %s complete, %f KB sent.\n",
	     path,totaln/1024.0);
    }
    close(localfd);
    break;
  }
}

static int process_user_line(char *line)
{
  char *paramstr, *linerecvd;
  uint32_t addr;
  uint16_t port;
  int n,code, nr;
  enum FTP_CMD cmd;
  struct sockaddr_in sa;
  socklen_t salen;
  if (pharsecmdline(line, &cmd, &paramstr) < 0) {
    fprintf(stderr, "wrong command format: %s\n", line);
    return;
  }

  switch (cmd) {
  case CMD_ABOR: 		/* sig_int will handle it */
  case CMD_PORT:		/* client will never need to handle it */
  case CMD_PASS:
  case CMD_USER:
    break;
  case CMD_QUIT:
  case CMD_SYST:
  case CMD_TYPE:
    sprintf(cntloutbuf, "%s\n",cmd2str(cmd));
    send(cntl_sockfd, cntloutbuf, strlen(cntloutbuf), 0);     
    lastcmd = cmd;
    break;
  case CMD_LIST:
  case CMD_RETR:
  case CMD_STOR:
    /* 
     * start data transfer  
     * 1) send PORT
     * 2) send command
     */
    data_serv_sockfd = init_data_server();
    addr = ntohl(curdataaddr);
    port = ntohs(curdataport);
    inet_ntop(AF_INET, &curdataaddr, cntloutbuf, LINELEN);
    printf("data addr: %s:%u\n", cntloutbuf, ntohs(curdataport));
    sprintf(cntloutbuf, "%s %d,%d,%d,%d,%d,%d\n", PORT,
	    (addr>>24) & 0xFF, (addr>>16) & 0xFF, 
	    (addr>>8) & 0xFF, (addr>>0) & 0xFF,
	    (port>>8) & 0xFF, (port>>0) & 0xFF);
    send(cntl_sockfd, cntloutbuf, strlen(cntloutbuf), 0); /* send PORT */
    printf("--> %s", cntloutbuf);

    data_sockfd = accept(data_serv_sockfd, (struct sockaddr*)&sa, &salen);
    if (data_sockfd < 0) {
      fprintf(stderr, "error on accept data connection: %s\n",
	      strerror(errno));
      return -1;
    }
    /* PORT response */
    n = readline(cntl_sockfd, cntlinbuf, LINELEN,
		 &cntlinbufused, &linerecvd);
    if (linerecvd == NULL && n == 0) {
      fprintf(stderr, "server closed\n");
      return 0;
    } else if (n < 0) {
      fprintf(stderr, "error on read data connection: %s\n",
	      strerror(errno));
      return -1;
    }
    printf("%s\n", linerecvd);
    code = atoi(linerecvd);
    free(linerecvd);
    if (code > 225)
      break;


    /* send command */
    n = sprintf(cntloutbuf, "%s\n", cmd2str(cmd));
    if (paramstr)
      sprintf(cntloutbuf+n-1, " %s\n", paramstr);
    send(cntl_sockfd, cntloutbuf, strlen(cntloutbuf), 0); 
    printf("--> %s", cntloutbuf);

    /* command response */
    n = readline(cntl_sockfd, cntlinbuf, LINELEN,
		 &cntlinbufused, &linerecvd);
    if (linerecvd == NULL && n == 0) {
      fprintf(stderr, "server closed\n");
      return 0;
    } else if (n < 0) {
      fprintf(stderr, "error on read data connection: %s\n",
	      strerror(errno));
      return -1;
    }
    printf("%s\n", linerecvd);
    code = atoi(linerecvd);
    free(linerecvd);
    if (code != 125)
      break;

    lastcmd = cmd;
    if (lastparams)
      free(lastparams);
    lastparams = paramstr;

    transfer_data();
    shutdown(data_sockfd, SHUT_RDWR);
    close(data_sockfd);
    data_sockfd = -1;
    break;
  }
}
static int process_cntl_line(char *line)
{
  char *delims = " \r\n";
  char *codestr, *mesgstr;
  int code;
  printf("%s\n",line);

  codestr = strtok(line, delims);
  mesgstr = strtok(NULL, delims);
  code = atoi(codestr);  
  return code;
}
static int do_client()
{
  fd_set rfds;
  int n;
  char *linerecvd;
  for (;;) {
    printf("ftp>");
    n = 0;
    //readline(STDIN_FILENO, stdinbuf, LINELEN, &n, &linerecvd);
    fgets(stdinbuf, LINELEN, stdin);
    linerecvd = strtok(stdinbuf, "\r\n");
    process_user_line(linerecvd);
    //    free(linerecvd);

#if 0
    n = readline(cntl_sockfd, cntlinbuf, LINELEN, &cntlinbufused, &linerecvd);
    if (linerecvd == NULL && n == 0) {		/* socket closed */
      close(cntl_sockfd);
      printf("control connection closed\n");
      return 0;
    } else if (n < 0) {
      fprintf(stderr, "error reveive data : %s\n", strerror(errno));
      return -1;
    }
    // process_cntl_line(linerecvd);
    //free(linerecvd);
#endif
  }
}
int main(int argc, const char *argv[])
{
  if (argc != 2) {
    fprintf(stdout, "usage: ftp host\n");
    exit(0);
  }
  peername = argv[1];
  gethostname(localname, HOST_NAME_MAX+1);

  strcpy(homedir, "/home/ftpdir/");
  strcpy(ftpdir, "/home/ftpdir/ftptmp/");

  init_cntl_connection();

  fprintf(stdout, "connection initialized\n");

  if (getlogin_r(username, LOGIN_NAME_MAX+1) < 0) {
    strcpy(username,"");
  }
  logon();
  passwd();
  lastcmd = CMD_USER;
  do_client();
  return 0;
}
