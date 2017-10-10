#########################################################################
# File Name: nut.sh
# Author: TruJrong
# mail: JrongTru@outlook.com
# Created Time: 2017年10月10日 星期二 17时27分22秒
#########################################################################
#!/bin/bash


NUT_PREFIX=/usr/local/ups

case $1 in
     start  )

          ${NUT_PREFIX}/sbin/upsdrvctl start santak
          ${NUT_PREFIX}/sbin/upsd -u root
          ${NUT_PREFIX}/sbin/upsmon
          touch /var/lock/subsys/upsd
          ;;

     stop  )

          ${NUT_PREFIX}/sbin/upsmon -c stop
          ${NUT_PREFIX}/sbin/upsd -c stop
          ${NUT_PREFIX}/sbin/upsdrvctl stop
          rm -f /var/lock/subsys/upsd
          ;;

     restart  )

          $0 stop
          $0 start
          ;;

     esac
