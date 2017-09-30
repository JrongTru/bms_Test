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

#define SANTAK_DEBUG 0  /* added */
#define SERVER_IP "172.16.134.221"
#define SERVER_PORT 23333
#define CONFIRM_SIG "1"
#define SHUT_SER "shutdown"
#define SANTAK_SEND_Q6 "Q6\r"
#define SANTAK_SEND_WA "WA\r"
#define SANTAK_SEND_SHUT "S.2\r"

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

enum Mode{
     PowerOn = 0, Standly, Bypass, Line, Bat, BatTest, Fault, Converter, HE, Shutdown };

static	float	lowvolt = 0, highvolt = 0;
static int ser_fd = 0;
static struct sockaddr_in serv_addr;

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
          ret = ser_send_pace(upsfd, UPSDELAY, "(S.2\r");

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
#endif
          printf("Exiting!\n");
          sleep(1);
          return 0;
     }

     return ret;
}

static void ups_sync(void)
{
     char	buf[256];
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
#endif

     memset(&serv_addr, 0, sizeof(serv_addr));
     serv_addr.sin_family = AF_INET;
     serv_addr.sin_port = htons(SERVER_PORT);
     inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr.s_addr);

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
     int	i, sys_mode;
     char	buf[256];
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

          sscanf(buf, "%*c%*f %*f %*f %*f %*f %*f %*f %*f %*f %*f %*f %*f %*f %*f %*d %*d %d %*d %*s %*s %*s", &sys_mode);

#if SANTAK_DEBUG
          printf("system on mode: [%d]\n", sys_mode);
#endif
          if(Line == sys_mode)
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
#if 0
     printf("The UPS will shut down in 12s.\n");

     if (ups_on_line())
          printf("The UPS will restart in about one minute.\n");
     else
          printf("The UPS will restart when power returns.\n");
#endif
     close_server();

     if(ups_on_line() && (atoi(dstate_getinfo("ups.work.mode")) == Line))
     {
          shutdown_ups();
     }
     else if(!ups_on_line())
     {
          close_server();
     }
     else
     {
          upslogx(LOG_INFO, "Ups has been shutdown!");
     }
}

static void send_Q6()                    /* added */
{
     float R_in_volt, ups_in_Hz, R_out_volt, ups_out_Hz, out_current, p_batt_volt, n_batt_volt, ups_temp;
     int remain_time, cap_percentage, sys_mode, bat_test_status;
     char fault_code[16], warn_code[16], buf[256]; /* modified */

     int ret = 0;

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
     write(STDOUT_FILENO, buf, strlen(buf));
     printf("\n");
#endif
     /*"(231.1 000.0 000.0 50.0 231.1 000.0 000.0 50.0 50 000 000 11.9 11.9 25.0 %d %d 3 2 NULL NULL YO\r"*/
     sscanf(buf, "%*c%f %*f %*f %f %f %*f %*f %f %f %*f %*f %f %f %f %d %d %d %d %s %s %*s", &R_in_volt, &ups_in_Hz, &R_out_volt,  &ups_out_Hz, &out_current, &p_batt_volt, &n_batt_volt, &ups_temp, &remain_time, &cap_percentage, &sys_mode, &bat_test_status, fault_code, warn_code);

#if SANTAK_DEBUG
     printf("%f %f %f %f %f %f %f %f %d %d %d %d %s %s.\n", R_in_volt, ups_in_Hz, R_out_volt, ups_out_Hz, out_current, p_batt_volt, n_batt_volt, ups_temp, remain_time, cap_percentage, sys_mode, bat_test_status, fault_code, warn_code);
#endif

     if(Fault == sys_mode)
     {
          dstate_setinfo("ups.work.mode", "%d", sys_mode);
          dstate_setinfo("ups.fault.code", "%s", fault_code);
          close_server();
          return;
     }

     if(remain_time > 360)
     {
          dstate_setinfo("battery.remain.time", "%d", remain_time);
     }
     else
     {
          status_set("LB");
          close_server();
          return;
     }

     if(cap_percentage > 40)
          dstate_setinfo("battery.charge", "%d", cap_percentage);
     else
     {
          status_set("LB");
          close_server();
          return;
     }

     dstate_setinfo("ups.R_input.voltage", "%f", R_in_volt);
     dstate_setinfo("ups.in.Hz", "%f", ups_in_Hz);
     dstate_setinfo("ups.R_output.voltage", "%f", R_out_volt);
     dstate_setinfo("output.current", "%f", out_current);
     dstate_setinfo("ups.out.Hz", "%f", ups_out_Hz);
     dstate_setinfo("battery.positive.voltage", "%f", p_batt_volt);
     dstate_setinfo("battery.negative.voltage", "%f", n_batt_volt);
     dstate_setinfo("ups.temperature", "%f", ups_temp);

     if(Line == sys_mode)
     {
          status_set("OL");
     }
     else if(Bypass == sys_mode)
     {
          if(R_in_volt < R_out_volt)
          {
               status_set("BOOST");
          }
          else
          {
               status_set("trim");
          }
     }
     else if(Bat == sys_mode)
     {
          status_set("OB");
     }

     dstate_setinfo("ups.work.mode", "%d", sys_mode);
     dstate_setinfo("battery.test.status", "%d", bat_test_status);

     return;

}

static void send_WA()               /* added */
{
     float R_out_power, total_power, R_out_ap_power, total_ap_power, out_current, out_load_percentage;
     char ups_status[64], buf[256]; /* modified */

     int ret = 0;

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
     printf("\n");
#endif
     /* (254.2 000.0 000.0 313.4 000.0 000.0 254.2 313.4 11.0 000.0 000.0 50 %s\r */
     sscanf(buf, "%*c%f %*f %*f %f %*f %*f %f %f %f %*f %*f %f %s", &R_out_power, &total_power, &R_out_ap_power, &total_ap_power, &out_current, &out_load_percentage, ups_status);

#if SANTAK_DEBUG
     printf("%.2f %.2f %.2f %.2f %.2f %.2f %s.\n", R_out_power, total_power, R_out_ap_power, total_ap_power, out_current, out_load_percentage, ups_status);
#endif

     if(ups_status[4] == '1')
     {
          close_server();
     }

     dstate_setinfo("R_output.power", "%f", R_out_power);
     dstate_setinfo("R_output.apparent.power", "%f", R_out_ap_power);
     dstate_setinfo("total.power", "%f", total_power);
     dstate_setinfo("apparent.total.power", "%f", total_ap_power);
     dstate_setinfo("output.current", "%f", out_current);
     dstate_setinfo("battery.load.percentage", "%f", out_load_percentage);
     dstate_setinfo("ups.status", "%s", ups_status);

     return;
}

void upsdrv_updateinfo(void)        /* modified */
{
     send_Q6();
     usleep(50000);
     send_WA();

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
}

void upsdrv_cleanup(void)
{
     ser_close(upsfd, device_path);
}
