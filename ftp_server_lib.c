#include "ftp_lib.h"
#include "ftp_server.h"
#define BUFLEN 8192
struct serv_cntl_conn *accept_new_client(int servfd)
{
  struct serv_cntl_conn *servconn;
  char addrstr[INET_ADDRSTRLEN];
  int flags;
  struct sockaddr_in peer;
  int peer_sa_len;	 
  int fd = accept(servfd, (struct sockaddr*)&peer, &peer_sa_len);

  flags = fcntl(fd, F_GETFL);
  fcntl(fd, F_SETFL, flags|O_NONBLOCK);
  servconn = malloc(sizeof(struct serv_cntl_conn));
  if (servconn == NULL) {
    fprintf(stderr, "malloc failed\n");
    return NULL;
  }
  memset(servconn, 0, sizeof(struct serv_cntl_conn));
  servconn->status = CS_WAIT_CMD;
  servconn->sockfd = fd;
  servconn->peeraddr = peer.sin_addr.s_addr;
  servconn->peerport = peer.sin_port;

  if (NULL == (inet_ntop(AF_INET, &servconn->peeraddr, 
			 addrstr, INET_ADDRSTRLEN))) {
    fprintf(stderr, "inet_ntop failed\n");
    return NULL;
  }
  fprintf(stdout, "accepted from %s:%u\n", 
	  addrstr, ntohs(servconn->peerport));
  return servconn;
}

int do_server_cmd_port(struct serv_cntl_conn *cntl, char *params)
{
  int a,b,c,d,e,f;
  int sockfd;
  in_addr_t addr;
  in_port_t port;
  char saddr[INET_ADDRSTRLEN];
  struct serv_data_conn *dconnp;

  sscanf(params,"%d,%d,%d,%d,%d,%d", &a, &b, &c, &d, &e, &f);
  port = e * 256 + f;
  sprintf(saddr, "%d.%d.%d.%d",a,b,c,d);
  inet_pton(AF_INET, saddr, &addr);
  cntl->peeraddr = addr;
  cntl->peerport = htons(port);
  cntl->status = CS_PORT_RECVD;
  printf("peer addr is %s:%d\n", saddr, port);

  /* create data connection */
  sockfd = connectserver(cntl->peeraddr, cntl->peerport,
			 localaddr, localdataport );
  if (sockfd < 0) {
    char addrstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &cntl->peeraddr, addrstr, INET_ADDRSTRLEN);
    fprintf(stderr, "coundn't connect client dataport %s:%u\n", 
	    addrstr,ntohs(cntl->peerport));
    inet_ntop(AF_INET, &localaddr, addrstr, INET_ADDRSTRLEN);
    fprintf(stderr, " using %s:%u\n", addrstr, ntohs(localdataport));
    fprintf(stderr, "\terror: %s\n", strerror(errno));
    return -1;
  }
  dconnp = malloc(sizeof(struct serv_data_conn));
  if (dconnp <= 0) {
    fprintf(stderr, "hand_request: malloc() failed!\n");
    return -1;
  }
  memset(dconnp,0,sizeof(struct serv_data_conn));
  dconnp->sockfd = sockfd;
  dconnp->status = DS_READY;
  dconnp->cmd = CMD_PORT;
  dconnp->filename = params? strdup(params) : NULL;
  if (cntl->dataconn) {
    if (cntl->dataconn->filename)
      free(cntl->dataconn->filename);
    free(cntl->dataconn);
  }
  cntl->dataconn = dconnp;

  return 0;
}
int do_server_cmd_transfer(struct serv_data_conn *dconnp)
{
  int localfd, totaln;
  int data_sockfd = dconnp->sockfd;
  char *filename = dconnp->filename;
  char path[LINELEN];
  pid_t pid;
  puts("transfering");
  if (!dconnp)
    return -1;
  if (filename == NULL) {
    strcpy(path, homedir);
  } else {
    snprintf(path, LINELEN, "%s%s", homedir, filename);
  }
  switch (dconnp->cmd) {
  case CMD_LIST:
    printf("ls %s\n", path);
    fflush(stdout);
    fflush(stderr);
    if ((pid = fork()) < 0) {
      fprintf(stderr, "fork error: %s\n", strerror);
      return -1;
    } else if (pid == 0) {
      /* 注意：这里的重定向只对子进程起作用 */
      if (dup2(data_sockfd, STDOUT_FILENO) != STDOUT_FILENO ||
	  dup2(data_sockfd, STDERR_FILENO) != STDERR_FILENO) {
	fprintf(stderr,"error %s\n", strerror(errno));
	return -1;
      }
      if (execl("/bin/ls", "","-l", path) < 0) {
	fprintf(stderr, "system error: %s\n", strerror(errno));
	exit(EXIT_FAILURE);
      }
      exit(EXIT_SUCCESS);			/* 子进程完 */
    }
    printf("fork() pid=%d\n", pid);
    dconnp->pid = pid;
#if 0
    if (waitpid(pid, NULL, 0) < 0) {
      fprintf(stderr, "wait pid %d error: %s\n",pid, strerror(errno));
      return -1;
    }
#endif
    return 0;
  case CMD_RETR:
    if (!filename) 
      return -1;
    localfd = open(path, O_RDONLY);
    if (localfd < 0) {
      fprintf(stderr, "error open file %s: %s\n", 
	      path, strerror(errno));
      return -1;
    }
    
    if ((pid = fork()) < 0) {
      fprintf(stderr, "fork error: %s\n", strerror);
      return -1;
    } else if (pid == 0) {
      totaln = copybetween2fd(data_sockfd, localfd);
      close(localfd);
      close(data_sockfd);
      if (abort_flag) {
	printf("aborted on retrive file %s\n", path);
	exit(EXIT_ABORT);
      } else {
	printf("retrive file %s complete, %.1f KB sent.\n",
	       path,totaln/1024.0);
	exit(EXIT_SUCCESS);
      }
    }
    printf("fork() pid=%d\n", pid);
    close(localfd);
    dconnp->pid = pid;
    return 0;
  case CMD_STOR:
    if (!filename) 
      return -1;
    localfd = open(path, O_CREAT|O_WRONLY);
    if (localfd < 0) {
      fprintf(stderr, "error open file %s: %s\n", 
	      path, strerror(errno));
      return -1;
    }
    if ((pid = fork()) < 0) {
      fprintf(stderr, "fork error: %s\n", strerror);
      return -1;
    } else if (pid == 0) {
      totaln = copybetween2fd(localfd, data_sockfd);
      if (abort_flag) {
	printf("aborted on store file %s\n", path);
	exit(EXIT_ABORT);
      } else {
	printf("store file %s complete, %.1f KB received\n",
	       path, totaln/1024.0);
	exit(EXIT_SUCCESS);
      }
    }
    printf("fork() pid=%d\n", pid);
    close(localfd);
    dconnp->pid = pid;
    return 0;
  default:
    return -1;
  }
}
