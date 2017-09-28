#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>

#define HMAX 4096
#define BMAX 1024
#define LTH 12

typedef struct{
     int fd;
     struct sockaddr_in adr;
}sockinfo;

void http_rp(sockinfo *, const char *, char *, long);
void get_info(int, char *, char * ,char *, char *);
char * get_name(char *);
void cth_sighup(int);
int build_socket(const char *, int);
void set_sigact(struct sigaction *, void (*)(int), int, ...);
void run_epoll(int);
void send_info(struct epoll_event *, int);
void send_file(struct epoll_event *, char *, long);
void send_dir(sockinfo *, char *);
void close_connection(sockinfo *, int);
const char * get_file_type(const char *);
int adjust_path(char *);
void accept_fd(int, int, int);

int main(int argc, const char * argv[])
{
     if(argc != 4)
     {
          printf("param error: ip, port, web path |\n");
          return -1;
     }

     chdir(argv[3]);

     int lfd = build_socket(argv[1], atoi(argv[2]));

     run_epoll(lfd);

     return 0;
}

int build_socket(const char * ip, int port)
{
     int lfd = socket(AF_INET, SOCK_STREAM, 0);
     struct sockaddr_in ser;
     ser.sin_family = AF_INET;
     inet_pton(AF_INET, ip, &ser.sin_addr.s_addr);
     ser.sin_port = htons(port);
     socklen_t len = sizeof(ser);

     int flag = 1;
     setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));

     bind(lfd, (struct sockaddr *)&ser, len);
     listen(lfd, BMAX-5);
     return lfd;
}

void run_epoll(int lfd)
{
     struct sigaction sa;
     sigemptyset(&sa.sa_mask);
     sigaddset(&sa.sa_mask, SIGHUP);
     sa.sa_handler = cth_sighup;
     sa.sa_flags = 0;

     sigaction(SIGHUP, &sa, NULL);

     struct epoll_event ev;
     ev.data.ptr = malloc(sizeof(sockinfo));
     memset(ev.data.ptr, 0, sizeof(sockinfo));
     ((sockinfo *)ev.data.ptr)->fd = lfd;
     ev.events = EPOLLIN;
     int epfd = epoll_create(BMAX);
     struct epoll_event evbuf[BMAX];

     epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);

     while(1)
     {
          int n, ret;
          socklen_t len = sizeof(struct sockaddr_in);

          printf("waiting for request...\n");
          ret = epoll_wait(epfd, evbuf, BMAX, -1);
          if(-1 == ret)
               continue;

          for(int i = 0; i < ret; ++i)
          {
               sockinfo * p = (sockinfo *)evbuf[i].data.ptr;
               if(!evbuf[i].events & EPOLLIN)
                    continue;
               if(p->fd == lfd)
               {
                    accept_fd(epfd, lfd, len);
               }
               else
               {
                    printf("accessing else block...\n");
                    ioctl(p->fd, FIONREAD, &n);
                    if(n)
                    {
                         send_info(&evbuf[i], epfd);
                    }
                    if(!n)
                    {
                         close_connection(p, epfd);
                    }
                    if(-1 == n)
                    {
                         close_connection(p, epfd);
                    }
               }
          }
     }	

}

void accept_fd(int epfd, int lfd, int len)
{
     int flag = 0;
     sockinfo s_if;
     struct epoll_event ev;
     ev.events = EPOLLIN|EPOLLET;

     char ip[64] = {0};

     s_if.fd = accept(lfd, (struct sockaddr *)&s_if.adr.sin_addr.s_addr, &len);
     
     flag = fcntl(s_if.fd, F_GETFL);
     flag |= O_NONBLOCK;
     fcntl(s_if.fd, F_SETFL, flag);

     ev.data.ptr = malloc(sizeof(s_if));
     memset(ev.data.ptr, 0, sizeof(s_if));
     memcpy(ev.data.ptr, &s_if, sizeof(s_if));

     epoll_ctl(epfd, EPOLL_CTL_ADD, s_if.fd, &ev);

     printf("connection built! ip: %s, port: %d|\n", inet_ntop(AF_INET, &s_if.adr.sin_addr.s_addr, ip, 64), ntohs(s_if.adr.sin_port));
     printf("performance!\n");
}

void send_info(struct epoll_event * p_ev, int epfd)
{
     printf("\n|---------------------------------------------------------------------|\n");
     sockinfo * psif = (sockinfo *)p_ev->data.ptr;
     struct stat st;
     char buf[BMAX], path[BMAX], method[LTH], protocol[LTH];
     
     printf("Information exchange! ip: %s, port: %d|\n", inet_ntop(AF_INET, &psif->adr.sin_addr.s_addr, buf, 64), ntohs(psif->adr.sin_port));

     get_info(psif->fd, buf, method, path, protocol);

     if(!memcmp(buf, "shutdown", 8))
     {
          write(psif->fd, "1", 2);
          close_connection(psif, epfd);
          exit(EXIT_SUCCESS);
     }


     lstat(path, &st);

     if(S_ISREG(st.st_mode))
          send_file(p_ev, path, (long)st.st_size);

     else if(S_ISDIR(st.st_mode))
          send_dir(psif, path);

     printf("\n^---------------------------------------------------------------------^\n");
}

void send_file(struct epoll_event *p_ev, char * path, long len)
{
     sockinfo * psif = (sockinfo *)p_ev->data.ptr;
     int n, fd;
     struct stat st;
     char buf[BMAX] = {0};
     printf("send file path: %s\n", path);
     lstat(path, &st);
     if((fd = open(path, O_RDONLY)) < 0 )
     {
          printf("read error: %s\n", strerror(errno));
          return;
     }
     http_rp(psif, get_file_type(path), "200 Ok", len);

     while(/*(p_ev->events & EPOLLOUT) && */(n = read(fd, buf, BMAX)))
     {
          if(n < 0)
          {
               printf("read error: %s", strerror(errno));
               return;
          }
          write(psif->fd, buf, len);
     }

     close(fd);
}

void send_dir(sockinfo * psif, char * path)
{
     char buf[HMAX] = {0};
     char pbuf[BMAX] = {0};
     struct dirent ** namelist;
     int ret = scandir(path, &namelist, NULL, alphasort);
     if(ret < 0)
     {
          printf("accessing directory error: %s\n", strerror(errno));
          return;
     }
     http_rp(psif, "text/html", "200 Ok", -1);
     struct stat st;

     sprintf(buf, "<!doctype html>\n<html><head><title>Viewing Directory</title></head>");
     sprintf(buf+strlen(buf), "<body><hr color=\"megna\" size=\"5\"></hr><h3>Directory: %s</h3><div align=\"left\"><h4 fontcolor=\"megna\">FileName</h4><div align=\"right\"><h4 fontcolor=\"megna\">Size</h4></div></div>", path);
     for(int i = 0; i < ret; ++i)
     {
          char * name = namelist[i]->d_name;
          sprintf(pbuf,"%s%s%s", path, (path[strlen(path)-1]=='/'? "":"/"), name);

#ifdef debug
          printf("name: %s\n", name);
          printf("path: %s\n", path);
          printf("pbuf: %s\n", pbuf);
#endif

          lstat(pbuf, &st);
          if(S_ISDIR(st.st_mode) && strcmp(name, ".") 
                    /*&& strcmp(name, "..")*/)
               sprintf(buf+strlen(buf), "<div align=\"left\"><a href=\"%s/\" title=\"%s\">%s</a></div>", pbuf, pbuf, name);
          if(S_ISREG(st.st_mode))
               sprintf(buf+strlen(buf), "<div align=\"left\"><a href=\"%s\" title=\"%s\">%s</a><div align=\"right\">%ld</div></div>", pbuf, pbuf, name, (long)st.st_size);
          if(send(psif->fd, buf, strlen(buf), 0)<0)
          {
               printf("send error: %s\n", strerror(errno));
               return;
          }
          memset(buf, 0, HMAX);
          memset(pbuf, 0, BMAX);
          free(namelist[i]);
     }
     sprintf(buf, "</body></html>");
     send(psif->fd, buf, strlen(buf), 0);
     free(namelist);
}

void close_connection(sockinfo * psif, int epfd)
{
     printf("Closing connection: %s!\n", strerror(errno));
     epoll_ctl(epfd, EPOLL_CTL_DEL, psif->fd, NULL);
     close(psif->fd);
     free(psif);
     psif = 0;
}

void http_rp(sockinfo * pif, const char * type, char * code, long len)
{
     printf("http responding type: %s\n", type);
     char buf[BMAX] = {0};
     sprintf(buf, "HTTP/1.1 %s\r\n", code);
     printf("http buf: %s\n", buf);

     write(pif->fd, buf, strlen(buf));

     sprintf(buf, "Content-Type: %s\r\n", type);
     sprintf(buf+strlen(buf), "Content-Length: %ld\r\n", len);
     printf("http buf: %s\n", buf);

     write(pif->fd, buf, strlen(buf));

     write(pif->fd, "\r\n", 2);
}

void get_info(int fd, char * buf, char * method, char * path, char * protocol)
{
     memset(buf, 0, BMAX);
     memset(path, 0, LTH);
     memset(method, 0, LTH);
     memset(protocol, 0, LTH);

     char pbuf[BMAX] = {0};

     read(fd, buf, BMAX);

     printf("connection info:\n%s\n", buf);

     if(!memcmp(buf, "shutdown", 8))
          return;

     sscanf(buf, "%[^ ] %[^ ] %[^ ]", method, pbuf, protocol);
     if(!strcmp(pbuf, "/"))
          strcpy(path, "web.html");
     else
     {
          strcpy(path, pbuf+1);
          if(path[strlen(path)-1] == '/') 
               path[strlen(path)-1] = 0;
     }
#if 1
     printf("pbuf: %s\n", pbuf);
     printf("method: %s\n", method);
     printf("path: %s\n", path);
     printf("protocol: %s\n", protocol);
#endif
}

#if 0
int adjust_path(char * path)
{
     if(!*path)
          return 0;
     char buf[BMAX] = {0};
     strcpy(buf, path);
     printf("\nadjust begin!\n");
     if(buf != (get_name(buf)-1))
          *(get_name(buf)-1) = 0;
     printf("adjust buf: %s\n", buf);
     int n = strlen(buf)/2;
     if(!strncmp(buf, &buf[n], n))
     {
          path[n] = 0;
          printf("adjust path: %s\n", path);
          return 1;
     }
     return 0;
}
#endif

char * get_name(char *str)
{
     char * tmp = &str[strlen(str)-1];
     while(*tmp != '/')
          --tmp;
     ++tmp;
     return tmp;
}

void get_dpath(char * path, char * dpath)
{
     char buf[BMAX] = {0};
     strcpy(buf, path);
     char * tmp = &path[strlen(buf) -1];
     while(*tmp != '/')
          --tmp;
     *tmp = 0;
     strcpy(dpath, buf);
}

void cth_sighup(int sig)
{
     return;
}

const char *get_file_type(const char *name)
{
     char* dot;

     dot = strrchr(name, '.');   
     if (dot == NULL)
          return "text/plain; charset=utf-8";
     if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0)
          return "text/html; charset=utf-8";
     if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0)
          return "image/jpeg";
     if (strcmp(dot, ".gif") == 0)
          return "image/gif";
     if (strcmp(dot, ".png") == 0)
          return "image/png";
     if (strcmp(dot, ".css") == 0)
          return "text/css";
     if (strcmp(dot, ".au") == 0)
          return "audio/basic";
     if (strcmp( dot, ".wav" ) == 0)
          return "audio/wav";
     if (strcmp(dot, ".avi") == 0)
          return "video/x-msvideo";
     if (strcmp(dot, ".mov") == 0 || strcmp(dot, ".qt") == 0)
          return "video/quicktime";
     if (strcmp(dot, ".mpeg") == 0 || strcmp(dot, ".mpe") == 0)
          return "video/mpeg";
     if (strcmp(dot, ".vrml") == 0 || strcmp(dot, ".wrl") == 0)
          return "model/vrml";
     if (strcmp(dot, ".midi") == 0 || strcmp(dot, ".mid") == 0)
          return "audio/midi";
     if (strcmp(dot, ".mp3") == 0)
          return "audio/mpeg";
     if (strcmp(dot, ".ogg") == 0)
          return "application/ogg";
     if (strcmp(dot, ".pac") == 0)
          return "application/x-ns-proxy-autoconfig";

     return "text/plain; charset=utf-8";
}
