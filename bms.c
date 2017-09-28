/*+--------------------------------------------------------+
  > File Name: bms.c
  > Author: TruJrong
  > Mail: JrongTru@outlook.com 
  > Created Time: 2017年09月21日 星期四 15时30分06秒
  +-------------------------------------------------------+*/

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <termios.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <sys/select.h>
#include <sys/time.h>
#include <ctype.h>

#define SAN_Q6 "(231.1 000.0 000.0 50.0 231.1 000.0 000.0 50.0 50 000 000 11.9 11.9 25.0 %d %d 3 2 NULL NULL YO\r"

#define SAN_WA "(254.2 000.0 000.0 313.4 000.0 000.0 254.2 313.4 11.0 000.0 000.0 50 %s\r"

#define SAN_PATH "/dev/ttyUSB1"

#define WAITING "BMS is running!\n"
#define GET_MSG "Get a msg: "
#define SENT "Return string (%d) has been sent!\n"
#define ERR_RECV "Something error occurred in reading msg!\n"

int set_comm_opt(int);
void do_communication(int);
int select_read(const int, void *, const size_t, const long, const long);
int select_write(const int, const void *, const size_t, const long, const long);
int get_line(int, char *, size_t);

int main(int argc,char *argv[])
{
     int bmsfd = open(SAN_PATH, O_RDWR);

     set_comm_opt(bmsfd);

     do_communication(bmsfd);

     return 0;
}

void do_communication(int bmsfd)
{
     int n = 0;
     char buf[128];
     int flag = 0;
     int remain_t = 450;
     int percentage = 60;
     do
     {
          write(STDOUT_FILENO, WAITING, strlen(WAITING));
          memset(buf, 0, sizeof(buf));

          n = get_line(bmsfd, buf, sizeof(buf));
          if(n > 0)
          {
               printf("%s\n", buf);

               if(!memcmp(buf, "Q6", 2))
               {
                    memset(buf, 0, sizeof(buf));
                    if(remain_t > 1 && percentage > 1)
                    {
                         sprintf(buf, SAN_Q6, remain_t, percentage);
                         remain_t -= 10;
                    }
                    else
                    {
                         printf("Low Battery Exiting!\n");
                         sleep(1);
                         break;
                    }
               }
               else if(!memcmp(buf, "WA", 2))
               {
                    memset(buf, 0, sizeof(buf));
                    if(flag >= 10)
                    {
                         sprintf(buf, SAN_WA, "10001100");
                         flag = 0;
                    }
                    else
                         sprintf(buf, SAN_WA, "00000100");
               }
               else if(!memcmp(buf, "(S", 2))
               {
                    int i = 3;
                    for(; i > 0; --i)
                    {
                         n = select_write(bmsfd, "1", 1, 3, 0);
                         if(n == 1)
                              break;
                    }
                    float remain = 0;
                    char tmp[16] = {0};
                    sscanf(buf, "(S%s", tmp);
                    if(tmp[0] == '.')
                    {
                         tmp[2] = tmp[1];
                         tmp[1] = tmp[0];
                         tmp[0] = '0';
                    }
                    remain = atof(tmp) * 60;
                    for(;remain > 0; --remain)
                    {
                         printf("Battery will be turned off with in %.2f s\n", remain);
                         sleep(1);
                    }
                    printf("BMS exits!\n");
                    break;
               }

               if (sizeof(buf) > 5)
               {
                    n = select_write(bmsfd, buf, strlen(buf), 3, 0);
                    ++flag;
               }
               else
                    continue;

          }
          else if(!n)
               continue;
          else
          {
               write(STDOUT_FILENO, ERR_RECV, strlen(ERR_RECV));
               break;
          }

          printf("%s\n", buf);
          printf(SENT, n);
          printf("+-------------------------------------------+\n");

     }while(1);

     close(bmsfd);
}

int set_comm_opt(int fd)
{
     int ret = 0;
     if(fd < 0)
     {
          write(STDOUT_FILENO, "fd err!\n", 8);
          return -1;
     }
     struct termios opt;
     if(tcgetattr(fd, &opt))
     {
          write(STDOUT_FILENO, "getopt err!\n", 12);
          return -1;
     }
#if 0
     opt.c_cflag &= ~CSIZE;
     opt.c_cflag |= CREAD;
     opt.c_cflag |= CS8;
     opt.c_iflag &= ~INPCK;
     opt.c_cflag &= ~PARENB;
     opt.c_cc[VMIN] = 1;
     opt.c_cc[VTIME] = 0;
#endif

#if 1 
     opt.c_cflag = CS8 | CLOCAL | CREAD;
     opt.c_iflag = IGNPAR;
     opt.c_oflag = 0;
     opt.c_lflag = 0;
     opt.c_cc[VMIN] = 1;
     opt.c_cc[VTIME] = 0;
#endif


     cfsetispeed(&opt, B2400);
     cfsetospeed(&opt, B2400);

     tcflush(fd, TCIOFLUSH);
     if(ret = tcsetattr(fd, TCSANOW, &opt))
          return ret;

     return ret;
}



/* Read up to buflen bytes from fd and return the number of bytes
 *    read. If no data is available within d_sec + d_usec, return 0.
 *       On error, a value < 0 is returned (errno indicates error). */
int select_read(const int fd, void *buf, const size_t buflen, const long d_sec, const long d_usec)
{
     int       ret;
     fd_set         fds;
     struct timeval tv; 

     FD_ZERO(&fds);
     FD_SET(fd, &fds);

     tv.tv_sec = d_sec;
     tv.tv_usec = d_usec;

     ret = select(fd + 1, &fds, NULL, NULL, &tv);

     if (ret < 1) {
          return ret;
     }   

     return read(fd, buf, buflen);
}

/* Write up to buflen bytes to fd and return the number of bytes
 *    written. If no data is available within d_sec + d_usec, return 0.
 *       On error, a value < 0 is returned (errno indicates error). */
int select_write(const int fd, const void *buf, const size_t buflen, const long d_sec, const long d_usec){
     int       ret;
     fd_set         fds;
     struct timeval tv;

     FD_ZERO(&fds);
     FD_SET(fd, &fds);

     tv.tv_sec = d_sec;
     tv.tv_usec = d_usec;

     ret = select(fd + 1, NULL, &fds, NULL, &tv);

     if (ret < 1) {
          return ret; 
     }

     return write(fd, buf, buflen);
}

int get_line(int fd, char * buf, size_t buflen)
{
     int i, ret;
     int count = 0;
     char tmp[32] = {0};

     memset(buf, 0, buflen);

     while(count < buflen)
     {
          ret = select_read(fd, tmp, sizeof(tmp), 3, 0);

          if(ret < 1)
               return ret;

          for(i = 0; i < ret; ++i)
          {
               if(count == buflen|| tmp[i] == '\r')
                    return count;

               buf[count++] = tmp[i];
          }
     }

     return count;
}
