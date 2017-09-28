
//Makelog.h 日志头文件



// Makelog.h

#ifndef _MAKE_LOG_H_
#define _MAKE_LOG_H_

/************************************************************************/
/* 
const char *file：文件名称
int line：文件行号
int level：错误级别
		0 -- 没有日志
		1 -- debug级别
		2 -- info级别
		3 -- warning级别
		4 -- err级别
int status：错误码
const char *fmt：可变参数
*/
/************************************************************************/
//实际使用的Level
extern int  MakeLevel[5];

void Make_Log(const char *file, int line, int level, int status, const char *fmt, ...);

#endif
