#include "ftp_lib.h"

#define MAX_CONN 64

#define EXIT_ABORT 10
extern in_addr_t localaddr;
extern in_port_t localcntlport;
extern in_port_t localdataport;
extern char homedir[];

enum serv_data_status_t {DS_READY, DS_TRANSING, DS_ABRTED, DS_FINISHED};
struct serv_data_conn
{
  int sockfd;
  pid_t pid;
  enum serv_data_status_t status;
  enum FTP_CMD cmd;
  char *filename;
};		   

/* 
   only one data connection
   while in TRANS_DATA status, only peek ABORT command to abort data transfer
*/
enum serv_cntl_status_t {CS_WAIT_CMD,
			 CS_PORT_RECVD, /* PORT received */
			 CS_WAIT_TRANS, /* wait to transfer data */
			 CS_TRANS_DATA,
			 CS_FINISHED};
struct serv_cntl_conn
{
  int sockfd;
  char *user;
  int logon;
  long logontime;
  long lasttime;
  in_addr_t peeraddr;
  in_port_t peerport;
  enum FTP_CMD lastcmd;
  char *lastparams;
  char line[LINELEN];
  int lineused;
  enum serv_cntl_status_t status;
  char *cwd;
  struct serv_data_conn *dataconn;
  struct serv_cntl_conn *next;
};

