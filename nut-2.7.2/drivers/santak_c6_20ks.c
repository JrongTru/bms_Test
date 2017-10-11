/* bestups.c - model specific routines for Best-UPS Fortress models

   Copyright (C) 1999  Russell Kroll <rkroll@exploits.org>

   ID config option by Jason White <jdwhite@jdwhite.org>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
   */

#include "main.h"
#include "serial.h"
#include <sys/types.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define DRIVER_NAME	"santak_c6_20ks"/* modified */
#define DRIVER_VERSION	"1.01" /* modified */

/*information of the test server*/
#define SANTAK_DEBUG 1  /* turn on debug print */
#define SERVER_IP "172.16.134.222"
#define SERVER_PORT 23333

/*custom signal*/
#define CONFIRM_SIG "1"
#define SHUT_SER "shutdown"

/*protocol of the ups santak-C6-20Ks*/
#define SANTAK_SEND_Q6 "Q6\r"
#define SANTAK_SEND_WA "WA\r"
#define SANTAK_SEND_SHUT "S.2\r"

#define STATUS_CODE_LEN 8
#define DATA_BUF_SZ 64
#define MAX_BUF_SZ 256

/* driver description structure */
upsdrv_info_t upsdrv_info = {
     DRIVER_NAME,
     DRIVER_VERSION,
     "Tru Jrong",
     DRV_STABLE,
     { NULL }
};

#define ENDCHAR  13	/* replies end with CR */
#define MAXTRIES 5
#define UPSDELAY 50000	/* 50 ms delay required for reliable operation */

#define SER_WAIT_SEC	3	/* allow 3.0 sec for ser_get calls */
#define SER_WAIT_USEC	0

static enum Mode{
     POWERON = 0, STANDLY, BYPASS, LINE, BAT, BATTEST, FAULT, CONVERTER, HE, SHUTDOWN, };

typedef struct santak_state{
     char key[DATA_BUF_SZ];
     char value[DATA_BUF_SZ];
}santak_state_t;

/*"(231.1 000.0 000.0 50.0 231.1 000.0 000.0 50.0 50 000 000 11.9 11.9 25.0 %d %d 3 2 NULL NULL YO\r"*/
typedef struct proto_Q6_info{
     santak_state_t R_in_volt;
     santak_state_t S_in_volt;
     santak_state_t T_in_volt;
     santak_state_t ups_in_Hz;
     santak_state_t R_out_volt;
     santak_state_t S_out_volt;
     santak_state_t T_out_volt;
     santak_state_t ups_out_Hz;
     santak_state_t R_out_current;
     santak_state_t S_out_current;
     santak_state_t T_out_current;
     santak_state_t p_batt_volt;
     santak_state_t n_batt_volt;
     santak_state_t ups_temp;
     santak_state_t remain_time;
     santak_state_t cap_percentage;
     santak_state_t sys_mode_n_bat_test_status;
     santak_state_t fault_code;
     santak_state_t warn_code;
     santak_state_t rear_data;
}proto_Q6_info_t;

typedef struct proto_WA_info{
     santak_state_t R_out_power;
     santak_state_t S_out_power;
     santak_state_t T_out_power;
     santak_state_t R_out_ap_power;
     santak_state_t S_out_ap_power;
     santak_state_t T_out_ap_power;
     santak_state_t total_power;
     santak_state_t total_ap_power;
     santak_state_t R_out_current;
     santak_state_t S_out_current;
     santak_state_t T_out_current;
     santak_state_t out_load_rate;
     santak_state_t ups_status;
}proto_WA_info_t;

static	float	lowvolt = 0, highvolt = 0;
static int ser_fd = 0;
static struct sockaddr_in serv_addr;
static proto_Q6_info_t * santak_info_Q6;
static proto_WA_info_t * santak_info_WA;

static void model_set(const char *abbr, const char *rating)
{
     if (!strcmp(abbr, "SAN")) {
          dstate_setinfo("ups.mfr", "%s", "Santak Power");
          dstate_setinfo("ups.model", "Santak %s", rating);
          return;
     }


     dstate_setinfo("ups.mfr", "%s", "Unknown");
     dstate_setinfo("ups.model", "Unknown %s (%s)", abbr, rating);

     printf("Unknown model detected - please report this ID: '%s'\n", abbr);
}

#if 0
static int instcmd(const char *cmdname, const char *extra)
{
     if (!strcasecmp(cmdname, "test.battery.stop")) {
          ser_send_pace(upsfd, UPSDELAY, "CT\r");
          return STAT_INSTCMD_HANDLED;
     }

     if (!strcasecmp(cmdname, "test.battery.start")) {
          ser_send_pace(upsfd, UPSDELAY, "T\r");
          return STAT_INSTCMD_HANDLED;
     }

     upslogx(LOG_NOTICE, "instcmd: unknown command [%s]", cmdname);
     return STAT_INSTCMD_UNKNOWN;
}
#endif

static void Santak_init_buffer()
{
     /* initialize the buffer for getting data responsed by Q6 protocol */
     santak_info_Q6 = (proto_Q6_info_t *)malloc(sizeof(proto_Q6_info_t));
     memset(santak_info_Q6, 0, sizeof(proto_Q6_info_t));

     strcpy(santak_info_Q6->sys_mode_n_bat_test_status.key, "sys.mode.and.battery.status");
     strcpy(santak_info_Q6->fault_code.key, "ups.fault.code");
     strcpy(santak_info_Q6->warn_code.key, "ups.warn.code");

     strcpy(santak_info_Q6->remain_time.key, "battery.remain.time");
     strcpy(santak_info_Q6->cap_percentage.key, "battery.charge");

     strcpy(santak_info_Q6->R_in_volt.key, "ups.R.input.voltage");
     strcpy(santak_info_Q6->S_in_volt.key, "ups.S.input.voltage");
     strcpy(santak_info_Q6->T_in_volt.key, "ups.T.input.voltage");

     strcpy(santak_info_Q6->R_out_volt.key, "ups.R.output.voltage");
     strcpy(santak_info_Q6->S_out_volt.key, "ups.R.output.voltage");
     strcpy(santak_info_Q6->T_out_volt.key, "ups.R.output.voltage");

     strcpy(santak_info_Q6->ups_in_Hz.key, "ups.in.Hz");
     strcpy(santak_info_Q6->ups_out_Hz.key, "ups.out.Hz");

     strcpy(santak_info_Q6->R_out_current.key, "ups.R.out.current");
     strcpy(santak_info_Q6->S_out_current.key, "ups.S.out.current");
     strcpy(santak_info_Q6->T_out_current.key, "ups.T.out.current");

     strcpy(santak_info_Q6->p_batt_volt.key, "battery.positive.voltage");
     strcpy(santak_info_Q6->n_batt_volt.key, "battery.negative.voltage");

     strcpy(santak_info_Q6->ups_temp.key, "ups.temperature");
     /*strcpy(santak_info_Q6->bat_test_status.key, "battery.test.status");*/
     strcpy(santak_info_Q6->rear_data.key, "protocol.rear.data");

     /* initialize the buffer for getting data responsed by WA protocol */
     santak_info_WA = (proto_WA_info_t *)malloc(sizeof(proto_WA_info_t));
     memset(santak_info_WA, 0, sizeof(proto_WA_info_t));

     strcpy(santak_info_WA->R_out_power.key, "ups.R.output.power");
     strcpy(santak_info_WA->S_out_power.key, "ups.S.output.power");
     strcpy(santak_info_WA->T_out_power.key, "ups.T.output.power");

     strcpy(santak_info_WA->total_power.key, "ups.total.output.power");

     strcpy(santak_info_WA->R_out_ap_power.key, "ups.R.output.apparent.power");
     strcpy(santak_info_WA->S_out_ap_power.key, "ups.S.output.apparent.power");
     strcpy(santak_info_WA->T_out_ap_power.key, "ups.T.output.apparent.power");

     strcpy(santak_info_WA->total_ap_power.key, "ups.total.output.apparent.power");

     strcpy(santak_info_WA->R_out_current.key, "ups.R.output.current");
     strcpy(santak_info_WA->S_out_current.key, "ups.S.output.current");
     strcpy(santak_info_WA->T_out_current.key, "ups.T.output.current");

     strcpy(santak_info_WA->out_load_rate.key, "ups.output.load.rate");

     strcpy(santak_info_WA->ups_status.key, "ups.status");
}

static int Santak_get_info(char * in_data, void * data_buf, int * out_len)
{
     int i = 0;
     char * tmp = in_data;
     unsigned long offset = sizeof(santak_state_t);

     /*"(231.1 000.0 000.0 50.0 231.1 000.0 000.0 50.0 50 000 000 11.9 11.9 25.0 %d %d 3 2 NULL NULL YO\r"*/
     while(*(++tmp))
     {
          unsigned long j = 0;

          while(*tmp!=' ' && *tmp!='\t')
          {
               if(!*tmp)
               {
                    *out_len = i;
                    return 0;
               }

               memcpy((char *)((unsigned long)data_buf + offset*i + DATA_BUF_SZ + j), tmp, 1);
               ++tmp;
               ++j;
          }
          if(!*(char *)((unsigned long)data_buf + offset*i + DATA_BUF_SZ))
          {
               return -1;
          }
          ++i;
     }

     return 0; 
}

static void print_data(void *data, int data_len)
{
     int i = 0;
     unsigned long offset = sizeof(santak_state_t);

     for(i = 0; i <= data_len; ++i)
     {
          printf("%s: %s\n", (char *)((unsigned long)data+offset*i), (char *)((unsigned long)data+offset*i+DATA_BUF_SZ));
     }
}

static int close_server()
{
     int i = 0;
     int ret = 0;
     char buf[256] = {0};
     for(; i < MAXTRIES; ++i)
     {
          if(!ser_fd || ser_fd < 0)
          {
               ser_fd = socket(AF_INET, SOCK_STREAM, 0);
               if(ser_fd < 0)
               {
                    upslogx(LOG_INFO, "Create socket file descriptor err! [%s]", strerror(errno));
                    ret = errno;
                    continue;
               }
               ret = 0;
          }
          if(connect(ser_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)))
          {
               upslogx(LOG_INFO, "connect to socket failed![%d]", errno);
#if SANTAK_DEBUG
               printf("WA connect to socket failed![%s]", strerror(errno));
#endif
               ret = -1;
               continue;
          }
          ret = write(ser_fd, SHUT_SER, strlen(SHUT_SER)+1);
          if(ret != 9)
          {
               upslogx(LOG_INFO, "write to socket failed![%d]", errno);
               ret = -1;
               continue;
          }
          else
          {
               ret = read(ser_fd, buf, sizeof(buf));
               if(ret <= 0)
               {
                    continue;
               }

               if(memcmp(buf, CONFIRM_SIG, strlen(CONFIRM_SIG)))
               {
                    continue;
               }

               close(ser_fd);
               ser_fd = 0;
#if SANTAK_DEBUG
               printf("Server exiting!\n");
#endif
          }

          ret = ser_send_pace(upsfd, UPSDELAY, SANTAK_SEND_SHUT);

          usleep(200000);
          ret = ser_get_line(upsfd, buf, sizeof(buf), ENDCHAR, "", SER_WAIT_SEC, SER_WAIT_USEC);
          if(buf[0] != '1')
          {
               continue;
          }
#if SANTAK_DEBUG
          printf("%s\n", buf);
#endif
          upslogx(LOG_INFO, "Server closed.");
          sleep(1);
          return 0;
     }

     return ret;
}

static int shutdown_ups()
{
     int i = 0;
     char buf[256] = {0};
     int ret = 0;
     for(i = 0; i < MAXTRIES; ++i)
     {
          ret = ser_send_pace(upsfd, UPSDELAY, SANTAK_SEND_SHUT);

          usleep(200000);
          ret = ser_get_line(upsfd, buf, sizeof(buf), ENDCHAR, "", SER_WAIT_SEC, SER_WAIT_USEC);
          if(ret > 0)
          {
               if(memcmp(buf, CONFIRM_SIG, 1))
               {
                    continue;
               }
          }
#if SANTAK_DEBUG
          printf("%s\n", buf);
          printf("Exiting!\n");
#endif
          sleep(1);
          return 0;
     }

     return ret;
}

static void ups_sync(void)
{
     char	buf[MAX_BUF_SZ] = {0};
     int	i, ret;

     for (i = 0; i < MAXTRIES; i++) {
          ser_send_pace(upsfd, UPSDELAY, SANTAK_SEND_Q6);/* modified */

          ret = ser_get_line(upsfd, buf, sizeof(buf), ENDCHAR, "", 
                    SER_WAIT_SEC, SER_WAIT_USEC);

#if SANTAK_DEBUG
          if(ret > 0)
          {
               printf("%s\n", buf);
          }
#endif 

          /* return once we get something that looks usable */
          if ((ret > 0) && (buf[0] == '('))
          {
               return;
          }

          usleep(250000);
     }

     fatalx(EXIT_FAILURE, "Unable to detect a Santak protocol UPS");/* modified */
}

void upsdrv_initinfo(void)
{
     highvolt = 273;
     lowvolt = 173;
     ups_sync();

     model_set("SAN", "MT 500pro");
#if 0
     ser_fd = socket(AF_INET, SOCK_STREAM, 0);
     if(ser_fd < 0)
          fatalx(EXIT_SUCCESS, "Create fd to server failed! [%d]", errno);

     memset(&serv_addr, 0, sizeof(serv_addr));
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_port = htons(SERVER_PORT);
     inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr.s_addr);
#endif

     printf("Detected %s %s on %s\n", dstate_getinfo("ups.mfr"), 
               dstate_getinfo("ups.model"), device_path);
#if 0
     upsh.instcmd = instcmd;

     dstate_addcmd("test.battery.start");
     dstate_addcmd("test.battery.stop");

#endif
}

static int ups_on_line(void)
{
     int	i, sys_mode, len;
     char	buf[MAX_BUF_SZ] = {0};
     int ret = 0;

     for (i = 0; i < MAXTRIES; i++) {
          ser_send_pace(upsfd, UPSDELAY, SANTAK_SEND_Q6);

          ret = ser_get_line(upsfd, buf, sizeof(buf), ENDCHAR, "", 
                    SER_WAIT_SEC, SER_WAIT_USEC);
          if(ret < 0)
          {
               upslogx(LOG_WARNING, "Detect ups on line error! [%s]", strerror(errno));
               continue;
          }

          ret = Santak_get_info(buf, santak_info_Q6, &len);
          if(ret)
          {
               upslogx(LOG_WARNING, "get information from buffer error!");
               continue;
          }

          santak_info_Q6->sys_mode_n_bat_test_status.value[1] = '\0';
          sys_mode = atoi(santak_info_Q6->sys_mode_n_bat_test_status.value);

#if SANTAK_DEBUG
          printf("\nsystem on mode: [%d]\n", sys_mode);
#endif
          if(LINE == sys_mode)
          {
               return 1;	/* on line */
          }

          return 0;	/*on battery */

          sleep(1);
     }

     upslogx(LOG_ERR, "Status read failed: assuming on battery");

     return 0;	/* on battery */
}	

void upsdrv_shutdown(void)
{
     /*close_server();*/
     int ret = 0;

     if(ups_on_line() && (atoi(dstate_getinfo("ups.work.mode")) == LINE))
     {
          ret = shutdown_ups();
          if(ret)
          {
               upslogx(LOG_ERR, "Ups shutdown failed with code: [%d]", ret);
          }
     }
#if 0
     else if(!ups_on_line())
     {
          close_server();
     }
#endif
     else
     {
          upslogx(LOG_INFO, "Ups has been shutdown!");
     }
}

static void Santak_send_Q6()                    /* added */
{
     int ret = 0;
     char buf[MAX_BUF_SZ] = {0};

     ret = ser_send_pace(upsfd, UPSDELAY, SANTAK_SEND_Q6);

     if (ret < 1) {
          ser_comm_fail("ser_send_pace failed");
          dstate_datastale();
          return;
     }

     /* these things need a long time to respond completely */
     usleep(200000);

     ret = ser_get_line(upsfd, buf, sizeof(buf), ENDCHAR, "", 
               SER_WAIT_SEC, SER_WAIT_USEC);
     if (ret < 1) {
          ser_comm_fail("Poll failed: %s", ret ? strerror(errno) : "timeout");
          dstate_datastale();
          return;
     }

     if (buf[0] != '(') {
          ser_comm_fail("Poll failed: invalid start character (got %02x)",
                    buf[0]);
          dstate_datastale();
          return;
     }

     ser_comm_good();

#if SANTAK_DEBUG
     printf("%s\n", buf);
#endif

     int out_len = 0;
     /*"(231.1 000.0 000.0 50.0 231.1 000.0 000.0 50.0 50 000 000 11.9 11.9 25.0 %d %d 3 2 NULL NULL YO\r"*/
     Santak_get_info(buf, santak_info_Q6, &out_len);

#if SANTAK_DEBUG
     print_data(santak_info_Q6, out_len);
#endif

     char tmp_buf[2] = {0};
     memset(tmp_buf, 0, 2);
     memcpy(tmp_buf, santak_info_Q6->sys_mode_n_bat_test_status.value, 1);
     int sys_mode = atoi(tmp_buf);
     dstate_setinfo("system.work.mode", "%d", sys_mode);

     if(FAULT == sys_mode)
     {
          dstate_setinfo(santak_info_Q6->fault_code.key, "%s", santak_info_Q6->fault_code.value);
#if 0
          close_server();
#endif
          return;
     }
     else if(LINE == sys_mode)
     {
          status_set("OL");
     }
     else if(BYPASS == sys_mode)
     {
          if(atof(santak_info_Q6->R_in_volt.value) < atof(santak_info_Q6->R_out_volt.value))
          {
               status_set("BOOST");
          }
          else
          {
               status_set("TRIM");
          }
     }
     else if(BAT == sys_mode)
     {
          status_set("OB");
     }
     else if(BYPASS == sys_mode)
     {
          status_set("BYPASS");
     }
     else if(STANDLY == sys_mode)
     {
          status_set("STANDLY");
     }

     dstate_setinfo(santak_info_Q6->cap_percentage.key, "%d", atoi(santak_info_Q6->cap_percentage.value));
     dstate_setinfo(santak_info_Q6->remain_time.key, "%d", atoi(santak_info_Q6->remain_time.value));
     if(BAT == sys_mode)
     {
          if(atoi(santak_info_Q6->remain_time.value) < 360 ||
               atoi(santak_info_Q6->cap_percentage.value) < 40)
          {
               status_set("LB");
          }
     }

     dstate_setinfo(santak_info_Q6->R_in_volt.key, "%.1f", atof(santak_info_Q6->R_in_volt.value));
     dstate_setinfo(santak_info_Q6->S_in_volt.key, "%.1f", atof(santak_info_Q6->S_in_volt.value));
     dstate_setinfo(santak_info_Q6->T_in_volt.key, "%.1f", atof(santak_info_Q6->T_in_volt.value));
     dstate_setinfo(santak_info_Q6->ups_in_Hz.key, "%.1f", atof(santak_info_Q6->ups_in_Hz.value));
     dstate_setinfo(santak_info_Q6->R_out_volt.key, "%.1f", atof(santak_info_Q6->R_out_volt.value));
     dstate_setinfo(santak_info_Q6->S_out_volt.key, "%.1f", atof(santak_info_Q6->S_out_volt.value));
     dstate_setinfo(santak_info_Q6->T_out_volt.key, "%.1f", atof(santak_info_Q6->T_out_volt.value));
     dstate_setinfo(santak_info_Q6->R_out_current.key, "%.1f", atof(santak_info_Q6->R_out_current.value));
     dstate_setinfo(santak_info_Q6->S_out_current.key, "%.1f", atof(santak_info_Q6->S_out_current.value));
     dstate_setinfo(santak_info_Q6->T_out_current.key, "%.1f", atof(santak_info_Q6->T_out_current.value));
     dstate_setinfo(santak_info_Q6->ups_out_Hz.key, "%.1f", atof(santak_info_Q6->ups_out_Hz.value));
     dstate_setinfo(santak_info_Q6->p_batt_volt.key, "%.1f", atof(santak_info_Q6->p_batt_volt.value));
     dstate_setinfo(santak_info_Q6->n_batt_volt.key, "%.1f", atof(santak_info_Q6->n_batt_volt.value));
     dstate_setinfo(santak_info_Q6->ups_temp.key, "%.1f", atof(santak_info_Q6->ups_temp.value));

     dstate_setinfo(santak_info_Q6->warn_code.key, "%s", santak_info_Q6->warn_code.value);
     dstate_setinfo(santak_info_Q6->rear_data.key, "%s", santak_info_Q6->rear_data.value);

     status_commit();
     dstate_dataok();

     return;

}

static void Santak_send_WA()               /* added */
{
     int ret = 0;
     char buf[MAX_BUF_SZ] = {0};

     ret = ser_send_pace(upsfd, UPSDELAY, SANTAK_SEND_WA);

     if (ret < 1) {
          ser_comm_fail("ser_send_pace failed");
          dstate_datastale();
          return;
     }

     /* these things need a long time to respond completely */
     usleep(200000);

     ret = ser_get_line(upsfd, buf, sizeof(buf), ENDCHAR, "", 
               SER_WAIT_SEC, SER_WAIT_USEC);
     if (ret < 1) 
     {
          ser_comm_fail("Poll failed: %s", ret ? strerror(errno) : "timeout");
          dstate_datastale();
          return;
     }

     if (buf[0] != '(') 
     {
          ser_comm_fail("Poll failed: invalid start character (got %02x)",
                    buf[0]);
          dstate_datastale();
          return;
     }
     else if(strlen(buf) != 77)
     {
          ser_comm_fail("information recive error, [%ld] character recieved.", strlen(buf));
          dstate_datastale();
          return;
     }

     ser_comm_good();

#if SANTAK_DEBUG
     printf("%s\n", buf);
     printf("\n");
#endif

     int out_len = 0;
     /* (254.2 000.0 000.0 313.4 000.0 000.0 254.2 313.4 11.0 000.0 000.0 50 %s\r */
     Santak_get_info(buf, santak_info_WA, &out_len);

#if SANTAK_DEBUG
     print_data(santak_info_WA, out_len);
#endif

     int i = 0;

     dstate_setinfo(santak_info_WA->ups_status.key, "%s", santak_info_WA->ups_status.value);

     for(i = 0; i < STATUS_CODE_LEN; ++i)
     {
          if('1' == santak_info_WA->ups_status.value[i])
          {
               if(0 == i)
               {
                    status_set("LINE ABNORMAL");
               }
               else if(1 == i)
               {
                    status_set("LB");
               }
               else if(2 == i)
               {
                    status_set("BYPASS");
               }
               else if(3 == i)
               {
                    status_set("UPS FAULT");
               }
               else if(4 == i)
               {
                    status_set("OB");
               }
               else if(5 == i)
               {
                    status_set("TESTING");
               }
               else if(6 == i)
               {
                    status_set("UPS OFF");
               }
          }
     }

     dstate_setinfo(santak_info_WA->R_out_power.key, "%.1f", atof(santak_info_WA->R_out_power.value));
     dstate_setinfo(santak_info_WA->S_out_power.key, "%.1f", atof(santak_info_WA->S_out_power.value));
     dstate_setinfo(santak_info_WA->T_out_power.key, "%.1f", atof(santak_info_WA->T_out_power.value));
     dstate_setinfo(santak_info_WA->R_out_ap_power.key, "%.1f", atof(santak_info_WA->R_out_ap_power.value));
     dstate_setinfo(santak_info_WA->S_out_ap_power.key, "%.1f", atof(santak_info_WA->S_out_ap_power.value));
     dstate_setinfo(santak_info_WA->T_out_ap_power.key, "%.1f", atof(santak_info_WA->T_out_ap_power.value));
     dstate_setinfo(santak_info_WA->total_power.key, "%.1f", atof(santak_info_WA->total_power.value));
     dstate_setinfo(santak_info_WA->total_ap_power.key, "%.1f", atof(santak_info_WA->total_ap_power.value));
     dstate_setinfo(santak_info_WA->R_out_current.key, "%.1f", atof(santak_info_WA->R_out_current.value));
     dstate_setinfo(santak_info_WA->S_out_current.key, "%.1f", atof(santak_info_WA->S_out_current.value));
     dstate_setinfo(santak_info_WA->T_out_current.key, "%.1f", atof(santak_info_WA->T_out_current.value));
     dstate_setinfo(santak_info_WA->out_load_rate.key, "%.1f", atof(santak_info_WA->out_load_rate.value));
     dstate_setinfo(santak_info_WA->ups_status.key, "%.1f", atof(santak_info_WA->ups_status.value));

     status_commit();
     dstate_dataok();
     return;
}

void upsdrv_updateinfo(void)        /* modified */
{
     Santak_send_Q6();
     usleep(50000);
     Santak_send_WA();

     status_init();
     status_commit();
     dstate_dataok();
}

void upsdrv_help(void)
{
}

void upsdrv_makevartable(void)
{
#if 0
     addvar(VAR_VALUE, "nombattvolt", "Override nominal battery voltage");
     addvar(VAR_VALUE, "battvoltmult", "Battery voltage multiplier");
#endif
     addvar(VAR_VALUE, "ID", "Force UPS ID response string");
}

void upsdrv_initups(void)
{
     upsfd = ser_open(device_path);
     ser_set_speed(upsfd, device_path, B2400);
     Santak_init_buffer();
}

void upsdrv_cleanup(void)
{
     ser_close(upsfd, device_path);
     if(santak_info_Q6)
     {
          free(santak_info_Q6);
          santak_info_Q6 = 0;
     }

     if(santak_info_WA)
     {
          free(santak_info_WA);
          santak_info_WA = 0;
     }
}
