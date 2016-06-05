#include "ftp_server.h"
#include <sys/select.h>
#include <sys/types.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <pthread.h>
static int servfd;
static int connlen = 0;
static struct serv_cntl_conn *cntl_head = NULL;
static int dataquene_sem;
in_addr_t localaddr;
in_port_t localcntlport;
in_port_t localdataport;
char homedir[PATH_MAX+1];
static char cntlsendbuf[LINELEN+1];

struct serv_cntl_conn *accept_new_client(int servfd);
int do_server_cmd_port(struct serv_cntl_conn *cntl, char *params);
void do_server_cmd_transfer(struct serv_data_conn *dconnp);

static void transfer_over(struct serv_cntl_conn *cntl)
{
  struct serv_data_conn *dconnp = cntl->dataconn;

  printf("close data sock: %d\n", dconnp->sockfd);
  /* 先shutdown是为了让客户端受到连接关闭的通知 */
  shutdown(dconnp->sockfd, SHUT_RDWR);
  close(dconnp->sockfd);
  cntl->status = CS_WAIT_CMD;
  dconnp->sockfd = -1;
  dconnp->pid = -1;  
}

void sig_child(int sig)
{
  pid_t pid = wait(NULL);
  struct serv_cntl_conn *cntl;
  for (cntl = cntl_head; cntl; cntl = cntl->next) {
    struct serv_data_conn *dconnp = cntl->dataconn;
    if (dconnp && dconnp->pid == pid) {
      transfer_over(cntl);
      break;
    }
  }
  if (signal(SIGCHLD, sig_child) == SIG_ERR) {
    fprintf(stderr, "signal error\n");
  }
  printf("sig child handled. pid=%d\n", pid);
}

/*
 init socket ,bind, listen
*/
static void init_server()
{
  struct addrinfo hints;
  struct addrinfo *result, *rp;
  int s, val;
  char hostname[HOST_NAME_MAX+1];
  gethostname(hostname, HOST_NAME_MAX+1);
  localdataport = htons(SERVER_DATA_PORT);

  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  //s = getaddrinfo(hostname, "ftp", &hints, &result);
  s = getaddrinfo("192.168.1.155", "ftp", &hints, &result);
  if(s != 0) {
    fprintf(stderr, "getaddinfo: %s\n", gai_strerror(s));
    exit(EXIT_FAILURE);
  }
  
  for (rp = result; rp != NULL; rp = rp->ai_next) {
    servfd = initserver(SOCK_STREAM, rp->ai_addr, rp->ai_addrlen, MAX_CONN);
    if (servfd >= 0){
      char addr[INET_ADDRSTRLEN];
      struct sockaddr_in * sa_in =  (struct sockaddr_in*)rp->ai_addr;
      in_addr_t in_addr = sa_in->sin_addr.s_addr;
      inet_ntop(AF_INET, &in_addr, addr, INET_ADDRSTRLEN);
      fprintf(stdout, "bind on address %s:%u\n", 
	      addr, ntohs(sa_in->sin_port));
      localaddr = in_addr;
      localcntlport = sa_in->sin_port;
      break;
    }
  }
  
  if (rp == NULL) {
    fprintf(stderr, "Counld not bind\n");
    exit(EXIT_FAILURE);
  }
  freeaddrinfo(result);
}

static void reset_cntl(struct serv_cntl_conn *cntl)
{
  if (cntl == NULL)
    return;
  cntl->lineused = 0;
  cntl->status = CS_WAIT_CMD;
}
int checkpasswd(char *user, char *passwd)
{
  //TODO: check password
  return 0;
}
static int handle_line_request(struct serv_cntl_conn *cntl, char *linerecvd)
{
  
  struct serv_data_conn *dconnp;
  int sockfd, n, bufsize, retval;
  char *params;
  enum FTP_CMD cmd;
  struct sembuf sems[] = {{0,1,0}};
  if (!cntl || !linerecvd)
    goto errout;
  puts("handle_line_request");
  retval = pharsecmdline(linerecvd, &cmd, &params);
  if (retval < 0) {
    fprintf(stderr, "wrong command: %s\n", linerecvd);
    goto errout;
  }

  fprintf(stdout, "line left --> [%d] %s\n", cntl->lineused, cntl->line);

  cntl->lastcmd = cmd;
  if (cntl->lastparams)
    free(cntl->lastparams);
  cntl->lastparams = params;
  switch(cmd) {
  case CMD_ABOR:    break;
  case CMD_LIST:    goto trans;
  case CMD_PASS:
    if (cntl->user) {
      if (checkpasswd(cntl->user, params) == 0) 
	  {
	snprintf(cntlsendbuf, LINELEN, "230 User logged in, proceed.\n");
	cntl->logon = 1;
      }
      else
	snprintf(cntlsendbuf, LINELEN, "530 Not logged in.\n");
    } else {
       snprintf(cntlsendbuf, LINELEN, "332 Need account for login.\n");
    }
    write(cntl->sockfd,cntlsendbuf,strlen(cntlsendbuf));
    puts(cntlsendbuf);
    break;
  case CMD_PORT:
    if (cntl->status == CS_WAIT_CMD) {
      if (do_server_cmd_port(cntl, params) == 0) {
	/* ready for transfer, send response for PORT command */
	snprintf(cntlsendbuf, LINELEN, 
		 "225 Data connection open; no transfer in progress.\n");
	write(cntl->sockfd,cntlsendbuf,strlen(cntlsendbuf));
	puts(cntlsendbuf);
	return 0;
      } else {
	goto errout;
      }
    } else {
      printf("receive PORT while status not CS_WAIT_CMD.\n");
      goto errout;
    }
    break;
  case CMD_QUIT:    break;
  case CMD_RETR:    goto trans;
  case CMD_STOR:    goto trans;
  case CMD_SYST:    break;
  case CMD_TYPE:    break;
  case CMD_USER:
    if (!params)
      break;
    if (cntl->user)
      free(cntl->user);
    cntl->user = strdup(params);
    cntl->logon = 0;
    snprintf(cntlsendbuf, LINELEN, "331 User name okay, need password.\n");
    write(cntl->sockfd,cntlsendbuf,strlen(cntlsendbuf));
    puts(cntlsendbuf);
    break;
  default:    goto errout;
  }
  /* 忽略的命令直接进入 */
 defaultout:
  cntl->lastcmd = cmd;
  cntl->status = CS_WAIT_CMD;
  return 0;
 trans:
  if (cntl->status == CS_PORT_RECVD && cntl->dataconn && 
      cntl->dataconn->status == DS_READY) {
    pid_t pid;
    struct serv_data_conn *dconnp = cntl->dataconn;
    dconnp->cmd = cmd;
    dconnp->filename = params ? strdup(params):NULL;
    snprintf(cntlsendbuf, LINELEN, 
	     "125 Data connection already open; transfer starting.\n");
    write(cntl->sockfd, cntlsendbuf, strlen(cntlsendbuf));
    printf("%s", cntlsendbuf);
    do_server_cmd_transfer(dconnp);
    //transfer_over(cntl);

    //cntl->status = CS_WAIT_CMD;
    cntl->status = CS_WAIT_TRANS;
    dconnp->status = DS_TRANSING;
    //    puts("data conn closed.");
    return 0;
  } else {
    goto errout;
  }
 errout:
  cntl->status = CS_WAIT_CMD;
  return -1;
}
static int handle_request(struct serv_cntl_conn *cntl)
{
  int n;
  char *linerecvd = NULL;
  if (!cntl)
    return -1;
  puts("handle_request");
  do {
    if (!linerecvd)
      free(linerecvd);
    n = readline(cntl->sockfd, cntl->line, LINELEN,
		 &cntl->lineused, &linerecvd);

    if (n == 0) { // connection closed
      close(cntl->sockfd);
      fprintf(stdout, "LOG: close socket %d\n", cntl->sockfd);
      cntl->sockfd = -1;
      cntl->status = CS_FINISHED;
      return 0;
    } else if (n < 0) {		/* error receive */
      if (errno == EAGAIN || errno == EWOULDBLOCK) /* no data */
	break;
      else {
	fprintf(stderr, "error receive data : %s\n", strerror(errno));
	reset_cntl(cntl);
	// exit(EXIT_FAILURE);
	return -1;
      }
    }
	
	if ((linerecvd != NULL)&&n>2) {
	  printf("\nn=%d\n",n);
	  printf("\n linerecvd =%s\n",linerecvd);
	  fprintf(stdout, "LOG: received line -->  %s\n", linerecvd);
      handle_line_request(cntl, linerecvd);
    }
  } while (n > 0 && linerecvd != NULL);
  return 0;  
}
/*
  1) select one fd
  2) accept to get connection, add the connection to pool
  3) read request, dispatch
*/
static void server()
{
  fd_set rfds;
  int retval, flags, maxfd = 0;
  struct serv_cntl_conn *curcntl, *lastcntl;


  /* set NONBLOCK */
  //  fcntl(servfd, F_SETOWN, getpid());
  flags = fcntl(servfd, F_GETFL);
  fcntl(servfd, F_SETFL, flags|O_NONBLOCK);

  fprintf(stdout,"server for loop\n");
  connlen = 0;
  cntl_head = NULL;
  for (;;) {
    FD_ZERO(&rfds);
    FD_SET(servfd, &rfds);
    maxfd = servfd;
    lastcntl = NULL;
    for (curcntl = cntl_head; curcntl; ) {
      /* 删除已经关闭的客户端 */
      if (curcntl->status == CS_FINISHED) {
	struct serv_cntl_conn *next = curcntl->next;
	if (lastcntl) {
	  lastcntl->next = next;
	} else {
	  cntl_head = next;
	}
	if (curcntl->lastparams)
	  free(curcntl->lastparams);
	if (curcntl->cwd)
	  free(curcntl->cwd);
	free(curcntl);
	curcntl = next;
	connlen --;
	continue;
      }
      int fd = curcntl->sockfd;
      FD_SET(fd, &rfds);
      maxfd = maxfd < fd ? fd : maxfd;
      lastcntl = curcntl;
      curcntl = curcntl->next;
    }

    while (select(maxfd+1, &rfds, NULL, NULL, NULL) == -1) {
      if (errno == EINTR) {
	continue;
      } else {
	fprintf(stderr,"select(): %s\n", strerror(errno));
	exit(EXIT_FAILURE);
      }
    }
    for (curcntl = cntl_head; curcntl; curcntl = curcntl->next) {
      int fd = curcntl->sockfd;
      if (FD_ISSET(fd, &rfds)) {
	fprintf(stdout,"fd %d is set\n", fd);
	handle_request(curcntl);
	printf("hand fd %d ok\n",fd);
      }
    }

    if (FD_ISSET(servfd, &rfds)) { /*new connect*/
      struct serv_cntl_conn *servconn;
      servconn = accept_new_client(servfd);
      if (servconn != NULL) {
	if (connlen < MAX_CONN) {
	  servconn->next = cntl_head;
	  cntl_head = servconn;
	  connlen ++;
	  fprintf(stdout, "accepted, fd = %d, connlen = %d\n", 
		  servconn->sockfd,connlen);
	} else {
	  fprintf(stderr, "reach max connection size\n");
	  shutdown(servconn->sockfd, SHUT_RDWR);
	  close(servconn->sockfd);
	  free(servconn);
	}
      }
    }
  }
}

int main(int argc, const char* argv[])
{
  int err;
  pthread_t tid;
  strcpy(homedir, "/home/ftpdir/");
  init_server();
  
#if 1
  if (signal(SIGCHLD, sig_child) == SIG_ERR) {
    fprintf(stderr, "signal error\n");
  }
#endif
  server();
  fprintf(stdout, "server out\n");
  return 0;
}
