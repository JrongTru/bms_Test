
#define  _CRT_SECURE_NO_WARNINGS 

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "make_log.h"

#define TRU_DEBUG_FILE_	"server.log"
#define TRU_MAX_STRING_LEN 		10240

//Level类别
#define NO_LOG_LEVEL		0
#define DEBUG_LEVEL			1
#define INFO_LEVEL			2
#define WARNING_LEVEL		3
#define ERROR_LEVEL			4

int  MakeLevel[5] = {NO_LOG_LEVEL, DEBUG_LEVEL, INFO_LEVEL, WARNING_LEVEL, ERROR_LEVEL};

//Level的名称
char MakeLevelName[5][10] = {"NOLOG", "DEBUG", "INFO", "WARNING", "ERROR"};

static int TRU_Error_GetCurTime(char* strTime)
{
	sTRUct tm*		tmTime = NULL;
	size_t			timeLen = 0;
	time_t			tTime = 0;	
	
	tTime = time(NULL);
	tmTime = localtime(&tTime);
	//timeLen = strftime(strTime, 33, "%Y(Y)%m(M)%d(D)%H(H)%M(M)%S(S)", tmTime);
	timeLen = strftime(strTime, 33, "%Y-%m-%d %H:%M:%S", tmTime);
	
	return timeLen;
}

static int TRU_Error_OpenFile(int* pf)
{
	char	fileName[1024];
	
	memset(fileName, 0, sizeof(fileName));
#ifdef WIN32
	sprintf(fileName, "c:\\TRU\\%s",TRU_DEBUG_FILE_);
#else
	sprintf(fileName, "%s/log/%s", getenv("HOME"), TRU_DEBUG_FILE_);
#endif
    
    *pf = open(fileName, O_WRONLY|O_CREAT|O_APPEND, 0666);
    if(*pf < 0)
    {
        return -1;
    }
	
	return 0;
}

static void TRU_Error_Core(const char *file, int line, int level, int status, const char *fmt, va_list args)
{
    char str[TRU_MAX_STRING_LEN];
    int	 strLen = 0;
    char tmpStr[64];
    int	 tmpStrLen = 0;
    int  pf = 0;
    
    //初始化
    memset(str, 0, TRU_MAX_STRING_LEN);
    memset(tmpStr, 0, 64);
    
    //加入LOG时间
    tmpStrLen = TRU_Error_GetCurTime(tmpStr);
    tmpStrLen = sprintf(str, "[%s] ", tmpStr);
    strLen = tmpStrLen;

    //加入LOG等级
    tmpStrLen = sprintf(str+strLen, "[%s] ", MakeLevelName[level]);
    strLen += tmpStrLen;
    
    //加入LOG状态
    if (status != 0) 
    {
        tmpStrLen = sprintf(str+strLen, "[ERRNO is %d] ", status);
    }
    else
    {
    	tmpStrLen = sprintf(str+strLen, "[SUCCESS] ");
    }
    strLen += tmpStrLen;

    //加入LOG信息
    tmpStrLen = vsprintf(str+strLen, fmt, args);
    strLen += tmpStrLen;

    //加入LOG发生文件
    tmpStrLen = sprintf(str+strLen, " [%s]", file);
    strLen += tmpStrLen;

    //加入LOG发生行数
    tmpStrLen = sprintf(str+strLen, " [%d]\n", line);
    strLen += tmpStrLen;
    
    //打开LOG文件
    if(TRU_Error_OpenFile(&pf))
	{
		return ;
	}
	
    //写入LOG文件
    write(pf, str, strLen);
    //Log_Error_WriteFile(str);
    
    //关闭文件
    close(pf);
    
    return ;
}


void Make_Log(const char *file, int line, int level, int status, const char *fmt, ...)
{
    va_list args;
	
	//判断是否需要写LOG
//	if(level!=DEBUG_LEVEL && level!=INFO_LEVEL && level!=WARNING_LEVEL && level!=ERROR_LEVEL)
	if(level == NO_LOG_LEVEL)
	{
		return ;
	}
	
	//调用核心的写LOG函数
    va_start(args, fmt);
    TRU_Error_Core(file, line, level, status, fmt, args);
    va_end(args);
    
    return ;
}
