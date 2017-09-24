/* based on //device/system/reference-ril/atchannel.c
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

/******************************************************************************
 * *(C) Copyright 2012 ZTE International Ltd.
 * * All Rights Reserved
 * *   EC:		000000000000		
 * *       Reason: clear AT rcv buffer, while reading from the begining
 * *       Date	20140609 zouxiaojie
 * ******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <cutils/properties.h>
#include <dirent.h>
#include <limits.h>


#include "zte-ril.h"
#include "atchannel.h"
#include "at_tok.h"

#include "misc.h"

#define LOG_TAG "RIL-AT"
#define MYLOG ALOGD
#define MAX_AT_RESPONSE   0x1000
#define NUM_ELEMS(x) (sizeof(x) / sizeof(x[0]))


#define  ENABLE_ONECHANNEL 1


typedef enum
{
    RECV_AT_CMD,
    RECV_SMS_PDU,
    SEND_AT_CMD,
    SEND_SMS_PDU
} ATLogType;

static pthread_t    s_tid_reader;
static ATUnsolHandler   s_unsolHandler;

/*add by renyimin for 8 channels to 1 channel 2015 9 21 */
static pthread_mutex_t s_ttyFDMutex = PTHREAD_MUTEX_INITIALIZER;

/* 通道 */
struct channel_struct
{
    int id;
    int ttyFd;
    pthread_mutex_t s_commandmutex; //used to assure just one AT cmd is processing  /* 命令互斥 */
    pthread_cond_t  s_commandcond;  /* 命令条件数 */
    ATCommandType   s_type;
    const char  *s_responsePrefix;  /* 响应前缀 */
    const char  *s_smsPDU; /* 短信 */
    ATResponse  *sp_response; /* 响应 */
    char    ATBuffer[MAX_AT_RESPONSE + 1];  /* buf最大的at响应 */
    char    *ATBufferCur; /* buf_cur???????? */
    int timeout_count;  /* 超时计数 */
};

#define STANDBYMODE_PROPERTY  "persist.radio.standbymode"

static struct channel_struct    *channels;
static int  COMMAND_CHANNEL_NUMBER;
static void (*s_onTimeout)(int channelID) = NULL;
static void (*s_onReaderClosed)(void) = NULL;
static int  s_readerClosed;
int reset_zte_ril;

static void onReaderClosed();
static int writeCtrlZ(struct channel_struct *chan, const char *s);
static int writeline(struct channel_struct *chan, const char *s);
static int get_cts_gpio_state();
static int getmode()
{
   char mode[PROPERTY_VALUE_MAX];
   property_get(STANDBYMODE_PROPERTY, mode, "1");
   if(0 != strcmp(mode,"1"))
   {   
	   return 0; 
   }
   else return 1;
 
}

static void AT_DUMP(ATLogType logType, const char *buff, int channel)
{
    int rilchannel = 0;
    if(getmode()==1)
   	{
   	 rilchannel = channel+15;
	}
	else {rilchannel = channel;}
   
	if (logType == SEND_AT_CMD)
    {
        ALOGD("===>>[Send AT cmd][%d] %s", rilchannel, buff);
    }
    else if (logType == SEND_SMS_PDU)
    {
        ALOGD("===>>[Send SMS PDU][%d] > %s^Z\n", rilchannel, buff);
    }
    else if (logType == RECV_AT_CMD)
    {
        ALOGD("<<====[Recv AT cmd][%d] %s", rilchannel, buff);
    }
    else if (logType == RECV_SMS_PDU)
    {
        ALOGD("<<====[Recv SMS PDU][%d] %s", rilchannel, buff);
    }
}

static void init_channel_struct(struct channel_struct *chan)
{
    chan->ttyFd = -1;
    pthread_mutex_init(&chan->s_commandmutex, NULL);
    pthread_cond_init(&chan->s_commandcond, NULL);
}

void init_all_channel_struct()
{
    int i;
    COMMAND_CHANNEL_NUMBER = get_channel_number();
    channels = calloc(COMMAND_CHANNEL_NUMBER, sizeof(struct channel_struct));
	MYLOG("\COMMAND_CHANNEL_NUMBER =  %d \r\n",COMMAND_CHANNEL_NUMBER);
    for (i = 0; i < COMMAND_CHANNEL_NUMBER; i++)
    {
    	/* 初始化通道结构体，也就是上面分配的channels */
        init_channel_struct(&channels[i]);
    }
}

/** add an intermediate response to sp_response*/
static void addIntermediate(struct channel_struct *chan, const char *line)
{
    ATLine  *p_new;

    p_new = (ATLine *) malloc(sizeof(ATLine));
    p_new->line = strdup(line);

    /* note: this adds to the head of the list, so the list
       will be in reverse order of lines received. the order is flipped
       again before passing on to the command issuer */
    p_new->p_next = chan->sp_response->p_intermediates;
    chan->sp_response->p_intermediates = p_new;
}

/**
 * returns 1 if line is a final response indicating error
 * See 27.007 annex B
 * WARNING: NO CARRIER are alyways unsolicited
 */
static int isFinalResponseError(const char *line)
{
    static const char   *s_finalResponsesError[] =
    {
        "ERROR",
        "+CMS ERROR:",
        "+CME ERROR:",
        "NO ANSWER",
        "NO DIALTONE",

    };
    size_t  i;

    for (i = 0; i < NUM_ELEMS(s_finalResponsesError); i++)
    {
        if (strStartsWith(line, s_finalResponsesError[i]))
        {
            return 1;
        }
    }

    return 0;
}

/**
 * returns 1 if line is a final response indicating success
 * See 27.007 annex B
 */
static int isFinalResponseSuccess(const char *line)
{
    static const char   *s_finalResponsesSuccess[] =
    {
        "OK",
        "CONNECT"   /* some stacks start up data on another channel */
    };
    size_t  i;

    for (i = 0; i < NUM_ELEMS(s_finalResponsesSuccess); i++)
    {
        if (strStartsWith(line, s_finalResponsesSuccess[i]))
        {
            return 1;
        }
    }

    return 0;
}

/**
 * returns 1 if line is a final response, either  error or success
 * See 27.007 annex B
 * WARNING: NO CARRIER and others are sometimes unsolicited
 */
static int isFinalResponse(const char *line)
{
    return (isFinalResponseSuccess(line) || isFinalResponseError(line));
}


/**
 * returns 1 if line is the first line in (what will be) a two-line
 * SMS unsolicited response
 */
static int isSMSUnsolicited(const char *line)
{
    static const char   *s_smsUnsoliciteds[] =
    {
        "+CMT:",
        "+CDS:",
        "+CBM:"
    };
    size_t  i;

    for (i = 0; i < NUM_ELEMS(s_smsUnsoliciteds); i++)
    {
        if (strStartsWith(line, s_smsUnsoliciteds[i]))
        {
            return 1;
        }
    }

    return 0;
}



//modify by renyimin
#if 0

static int isCallUnsolicited(const char *line)
{
    static const char   *s_callUnsoliciteds[] =
    {
        "+CRING:",
        "RING",
        "NO CARRIER",
        "+CCWA:",
        "+CSSI:",
        "+CSSU:",
        "+CUSD:",
        "+CGEV:",
        "+ZGIPDNS:",
        "+CLIP:",
        "+CNAP:",
        "CONNECT"
    };



    static const char   *s_otherUnsoliciteds[] =
    {
    "+CREG:",
    "+CGREG:",
    "^MODE:",
    "+ZMMI:",
    "+CCCM:",
    "+CLCC:",
    "^DSCI:",
    "DVTCLOSED",
    "+CMTI:",
    "+ZMGSF:",
    "+ZURDY:",
    "+ZUFCH:", 
    "+ZUSLOT:",
    "^ORIG:",
    "^CONF:",
    "^CONN:",
    "^CEND:",
    "^DSCI:",
    "+ZCPI:",
    "+ZICCID:", 
    "+ZMSRI",
    "+ZPBIC:",
    "+ZEMCI:",

 
    };
	
    size_t  i;

    for (i = 0; i < NUM_ELEMS(s_callUnsoliciteds); i++)
    {
        if (strStartsWith(line, s_callUnsoliciteds[i]))
        {
            return 1;
        }
    }
	
    for (i = 0; i < NUM_ELEMS(s_otherUnsoliciteds); i++)
    {
        if (strStartsWith(line, s_otherUnsoliciteds[i]))
        {
            return 1;
        }
    }

    return 0;
}
#else

/**
 * returns 1 if line is call related unsolicited msg. These msg should be sent within the call session/context,
 * so they are not sent via unsol channel
 */
static int isCallUnsolicited(const char *line)
{
    static const char   *s_callUnsoliciteds[] =
    {
        "+CRING:",
        "RING",
        "NO CARRIER",
        "+CCWA:",
        "+CSSI:",
        "+CSSU:",
        "+CUSD:",
        "+CGEV:",
        "+ZGIPDNS:",
        "+CLIP:",
        "+CNAP:",
        "CONNECT"
    };
    size_t  i;

    for (i = 0; i < NUM_ELEMS(s_callUnsoliciteds); i++)
    {
        if (strStartsWith(line, s_callUnsoliciteds[i]))
        {
            return 1;
        }
    }
    return 0;
}


#endif

/** assumes s_commandmutex is held */
static void handleFinalResponse(struct channel_struct *chan, const char *line)
{
    chan->sp_response->finalResponse = strdup(line);

    pthread_cond_signal(&chan->s_commandcond);
}

//modify by renyimin



#if 0

static int handleUnsolicited(struct channel_struct *chan, const char *line)
{
    if (modemType == TSP_CP && isCallUnsolicited(line))
    {
        if (s_unsolHandler != NULL)
        {
            s_unsolHandler(line, NULL);//onUnsolicited
		return 1;
        }
    }

	return 0;
}

#else
static void handleUnsolicited(struct channel_struct *chan, const char *line)
{
    if (chan->id == CHANNEL_UNSOLICITED || (modemType == TSP_CP && isCallUnsolicited(line)))
    {
        if (s_unsolHandler != NULL)
        {
            s_unsolHandler(line, NULL);
        }
    }
}


#endif



void handleSingleLinebyStype(struct channel_struct *chan, const char *line)
{
	MYLOG("\tintermediate SINGLELINE: line:%s, prefix:%s", line, chan->s_responsePrefix);
	if (chan->sp_response->p_intermediates == NULL && chan->s_responsePrefix && strStartsWith(line, chan->s_responsePrefix))
	{
		MYLOG("\tintermediate add");
		addIntermediate(chan, line);
	}
	else
	{
		/* we already have an intermediate response */
		handleUnsolicited(chan, line);
	}
}

void handleMultiLinebyStype(struct channel_struct *chan, const char *line)
{
	MYLOG("\tintermediate MULTILINE");
	if (strStartsWith(line, chan->s_responsePrefix))
	{
		addIntermediate(chan, line);
	}
	else
	{
		handleUnsolicited(chan, line);
	}
}

void handleLinebyStype(ATCommandType s_type, struct channel_struct *chan, const char *line)
{
	switch (chan->s_type)
	{
		case NO_RESULT:
		{
			MYLOG("\tintermediate NO RESULT, got unsolicited message");
			handleUnsolicited(chan, line);
			break;
		}
		case NUMERIC:
		{
			MYLOG("\tintermediate NUMERIC:%s", line);
			if (chan->sp_response->p_intermediates == NULL && isdigit(line[0]))
			{
				MYLOG("\tintermediate NUMERIC add intermediate");
				addIntermediate(chan, line);
			}
			else
			{
				/* either we already have an intermediate response or the line doesn't begin with a digit */
				MYLOG("\tintermediate NUMERIC unsolicitied");
				handleUnsolicited(chan, line);
			}
			break;
		}
		case SINGLELINE:
		{
			handleSingleLinebyStype(chan, line);
			break;
		}
		case MULTILINE:
		{
			handleMultiLinebyStype(chan, line);

			break;
		}
		case MULTILINE_SMS:
		{
			MYLOG("\tintermediate MULTILINE_SMS");			
			addIntermediate(chan, line);			
			break;
		}		
		default:
		{
			ALOGE("Unsupported AT command type %d\n", chan->s_type);
			handleUnsolicited(chan, line);
		}
	}
}

static void processLine(struct channel_struct *chan, const char *line)
{
    MYLOG("%s entry: process line: %s", __FUNCTION__, line);
    // NO CARRIER will be regarded as unsolicited message because all ATD will received OK after call-req-cnf
    pthread_mutex_lock(&chan->s_commandmutex);
	
    if (chan->sp_response == NULL || strStartsWith(line, "NO CARRIER"))
    {
        MYLOG("\t No command pending, it's unsolicited message");/* no command pending */
    	handleUnsolicited(chan, line);  /* 处理主动提供的 */
    }
    else if (isFinalResponseSuccess(line))
    {
    	MYLOG("\t It's  final response end ok");
        chan->sp_response->success = 1;
        handleFinalResponse(chan, line);  /* 处理最后的响应 */
    }
    else if (isFinalResponseError(line))
    {
        MYLOG("\t It's  final response end failed");
        chan->sp_response->success = 0;
        handleFinalResponse(chan, line);
    }
    else if (chan->s_smsPDU != NULL && 0 == strcmp(line, "> "))
    {
   	MYLOG("\t writeCtrlZ(chan, chan->s_smsPDU)");
        writeCtrlZ(chan, chan->s_smsPDU);/* See eg. TS 27.005 4.3  like AT+CMGS have a "> "  */
        chan->s_smsPDU = NULL;
    }
    else
    {
    	MYLOG("\t handleLinebyStype");
        handleLinebyStype(chan->s_type, chan, line);
    }
    MYLOG("%s exit", __FUNCTION__);
    pthread_mutex_unlock(&chan->s_commandmutex);
}


/**
 * Returns a pointer to the end of the next line
 * special-cases the "> " SMS prompt
 *
 * returns NULL if there is no complete line
 */
static char * findNextEOL(char *cur)
{
    if (cur[0] == '>' && cur[1] == ' ' && cur[2] == '\0')
    {
        /* SMS prompt character...not \r terminated */
        return cur + 2;
    }

    // Find next newline
    while (*cur != '\0' && *cur != '\r' && *cur != '\n')
    {
        cur++;
    }

    return *cur == '\0' ? NULL : cur;
}


/**
 * Reads reply from the AT channel, save reply in chan->ATBuffer.
 * Assumes it has exclusive read access to the FD
 */
static int readChannel(struct channel_struct *chan)
{
    MYLOG("%s entry,channelID:%d", __FUNCTION__, chan->id);
    ssize_t count;
    char    *p_read = NULL;
    if (*chan->ATBufferCur == '\0')
    {
        chan->ATBufferCur = chan->ATBuffer;/* empty buffer */
		memset(chan->ATBuffer, 0, sizeof(chan->ATBuffer));//clear AT rcv buffer, while reading from the begining zouxiaojie 20140609
        *chan->ATBufferCur = '\0';
        p_read = chan->ATBuffer;
    }
    else
    {
        int CurIdx = chan->ATBufferCur - chan->ATBuffer; /* there's data in the buffer from the last get_at_buffer_line*/
        ALOGW("%s ATBufferCur not empty, start at %d: %s", __FUNCTION__, CurIdx, chan->ATBufferCur);
        size_t  len = strlen(chan->ATBufferCur);
        if (CurIdx > 0)
        {
            memmove(chan->ATBuffer, chan->ATBufferCur, len + 1);
            chan->ATBufferCur = chan->ATBuffer;
        }
        p_read = chan->ATBufferCur + len;
    }
    if (0 == MAX_AT_RESPONSE - (p_read - chan->ATBuffer))
    {
        ALOGE("ERROR: Input line exceeded buffer\n");
        chan->ATBufferCur = chan->ATBuffer;/* ditch buffer and start over again */
		memset(chan->ATBuffer, 0, sizeof(chan->ATBuffer));////clear AT rcv buffer, while reading from the begining zouxiaojie 20140609
        *chan->ATBufferCur = '\0';
        p_read = chan->ATBuffer;
    }
    do
    {
#if ENABLE_ONECHANNEL
	 //pthread_mutex_lock(&s_ttyFDMutex);
#endif	
        MYLOG("begin read channel[%d], bufsize=%d", chan->id, MAX_AT_RESPONSE - (p_read - chan->ATBuffer));
        count = read(chan->ttyFd, p_read, MAX_AT_RESPONSE - (p_read - chan->ATBuffer));
        MYLOG("end read channel[%d], count=%ld", chan->id, count);
#if ENABLE_ONECHANNEL
	 //pthread_mutex_unlock(&s_ttyFDMutex);
#endif	
    }
    while (count < 0 && errno == EINTR);
    if (count > 0)
    {
        p_read[count] = '\0';
    }
    else if (count == 0)/* read error encountered or EOF reached */
    {
        ALOGE("atchannel: EOF reached");
    }
    else
    {
        ALOGE("atchannel: read error %s", strerror(errno));
    }
    return count;
}

/* get line from channel[channelID].ATBuffer
 * if not complete line in ATBuffer, return NULL.
 */
static const char * get_at_buffer_line(struct channel_struct *chan)
{
    MYLOG("%s entry,channelID:%d", __FUNCTION__, chan->id);
    ssize_t count;
    char    *p_eol = NULL;
    char    *ret = NULL;

    if (*chan->ATBufferCur == '\0')
    {
        return NULL;
    }

    // skip over leading newlines
    while (*chan->ATBufferCur == '\r' || *chan->ATBufferCur == '\n')
    {
        chan->ATBufferCur++;  /* 直到开头不是回车或者换行 */
    }
    p_eol = findNextEOL(chan->ATBufferCur);

    if (p_eol)
    {
        /* a full line in the buffer. Place a \0 over the \r and return */
        ret = chan->ATBufferCur;
        *p_eol = '\0';
        chan->ATBufferCur = p_eol + 1; /* this will always be <= p_read,    */
        /* and there will be a \0 at *p_read */
        while (*chan->ATBufferCur == '\r' || *chan->ATBufferCur == '\n')
        {
            chan->ATBufferCur++;
        }
    }
    return ret;
}

static void onReaderClosed()
{
    ALOGE("onReaderClosed");
    if (s_onReaderClosed != NULL && s_readerClosed == 0)
    {
        s_readerClosed = 1;
        int i;
        for (i = 0; i < COMMAND_CHANNEL_NUMBER; i++)
        {
            at_channel_close(i);
            struct channel_struct   *chan = &channels[i];
            pthread_mutex_lock(&chan->s_commandmutex);
            pthread_cond_signal(&chan->s_commandcond);
            pthread_mutex_unlock(&chan->s_commandmutex);
        }
        s_onReaderClosed();
    }
}

int processSmsUnsolicited(struct channel_struct *chan, const char *line)
{
    char    *line1;
    const char  *line2;
    MYLOG("found sms unsolicited on ChannelReader");

    // The scope of string returned by 'readline()' is valid only
    // till next call to 'readline()' hence making a copy of line
    // before calling readline again.
    line1 = strdup(line);
    line2 = get_at_buffer_line(chan);
    while (line2 == NULL)
    {
        if (readChannel(chan) <= 0)
        {
            free(line1);
            return -1;
        }
        line2 = get_at_buffer_line(chan);
    }
	//ALOGD(" === 333 bufantong");
    AT_DUMP(RECV_SMS_PDU, line2, chan->id);
	//ALOGD(" === 444 bufantong");
    if ((s_unsolHandler != NULL) && (chan->id == CHANNEL_UNSOLICITED))
    {
        s_unsolHandler(line1, line2);
    }

    free(line1);
	return 0;
}

static int channelReader(struct channel_struct *chan)
{
    const char  *line;
	int ret = -1;

    
    if (readChannel(chan) <= 0)
    {
        return -1;
    }
 

    while (1)
    {
        line = get_at_buffer_line(chan);
        if (line == NULL)
        {
            return 0;
        }
		//ALOGD(" === 555 bufantong");
        AT_DUMP(RECV_AT_CMD, line, chan->id);
		//ALOGD(" === 666 bufantong");
        if (isSMSUnsolicited(line))
        {
            ret = processSmsUnsolicited(chan, line);
			if(ret == -1)
			{
				return -1;
			}
        }
        else
        {
            processLine(chan, line);
        }
    }
    return 0;
}

int openFDSetTtyFd(fd_set *fdset)
{
    int maxfd = 0, i;
	
	FD_ZERO(fdset);
    for (i = 0; i < COMMAND_CHANNEL_NUMBER; i++)
    {
        if (channels[i].ttyFd > maxfd)
        {
            maxfd = channels[i].ttyFd;
        }
        if (channels[i].ttyFd != -1)
        {
            ALOGD("Open FD_SET ttyfd channels[%d].ttyFd=%d success", i, channels[i].ttyFd);
            FD_SET(channels[i].ttyFd, fdset);
        }
        else
        {
            ALOGD("Can not FD_SET ttyfd (channels[%d].ttyFd=%d, bad_channel", i, channels[i].ttyFd);
            return -1;//goto bad_channel;
        }
    }
	return maxfd;
}

static void * readerLoop(void *arg)
{
    pthread_setname_np(pthread_self(), "RIL-AT");
    ThreadContext   context = {"RIL-AT", -1, 0};
    setThreadContext(&context);
    int maxfd = 0, readSuccess, i, ret;
    fd_set  fdset, readset;

    maxfd = openFDSetTtyFd(&fdset);
    if(maxfd == -1)
    {
	    goto bad_channel;
    }
    for (; ;)/* loop to read multi-channel */
    {
        if (reset_zte_ril == 1)
        {
            ALOGD("reset_zte_ril");
            reset_zte_ril = 0;
            goto bad_channel;
        }
        readset = fdset;/* read multi-channel by select() */
		
        do
        {
            i = select(maxfd + 1, &readset, NULL, NULL, NULL);
        }
        while (i < 0 && errno == EINTR);
        if (i < 0)
        {
            break;
        }

        readSuccess = 1;  /* check which fd is readable, then read it by readline(fd) */
        for (i = 0; i < COMMAND_CHANNEL_NUMBER; i++)
        {
            struct channel_struct   *chan = &channels[i];
            if (!((chan->ttyFd != -1) && (FD_ISSET(chan->ttyFd, &readset))))
            {
                continue;
            }
			ret = channelReader(chan);
			if (ret < 0)
			{
				readSuccess = 0;
				goto bad_channel;
			}
        }


	  
    }
bad_channel:
    /* exit loop, and thread quit, callback s_onReaderClosed, which is set at_set_on_reader_closed, is called*/
    onReaderClosed();
    return NULL;
}

/**
 * Sends string s to the radio with a \r appended.
 * Returns AT_ERROR_* on error, 0 on success
 *
 * This function exists because as of writing, android libc does not
 * have buffered stdio.
 */
static int writeline(struct channel_struct *chan, const char *s)
{
    size_t  cur = 0;
    size_t  len = strlen(s);
    ssize_t written;
    int s_fd = chan->ttyFd;

    if (s_fd < 0 || s_readerClosed > 0)
    {
        return AT_ERROR_CHANNEL_CLOSED;
    }

    AT_DUMP(SEND_AT_CMD, s, chan->id);

    char    *t = (char  *) malloc(len + 1);
    memcpy(t, s, len);
    t[len++] = '\r';
    while (cur < len)
    {
        do
        {
            written = write(s_fd, t + cur, len - cur);
        }
        while (written < 0 && errno == EINTR);

        if (written < 0)
        {
            free(t);
            return AT_ERROR_GENERIC;
        }

        cur += written;
    }
    free(t);
    return 0;
}

static void clearPendingCommand(struct channel_struct *chan)
{
    if (chan->sp_response != NULL)
    {
        at_response_free(chan->sp_response);
    }

    chan->sp_response = NULL;
    chan->s_responsePrefix = NULL;
    chan->s_smsPDU = NULL;
}

static int writeCtrlZ(struct channel_struct *chan, const char *s)
{
    size_t  cur = 0;
    size_t  len = strlen(s);
    ssize_t written;
    int s_fd = chan->ttyFd;

    if (s_fd < 0 || s_readerClosed > 0)
    {
        return AT_ERROR_CHANNEL_CLOSED;
    }

    AT_DUMP(SEND_AT_CMD, s, chan->id);

    /* the main string */
    while (cur < len)
    {
        do
        {
            written = write(s_fd, s + cur, len - cur);
        }
        while (written < 0 && errno == EINTR);

        if (written < 0)
        {
            return AT_ERROR_GENERIC;
        }

        cur += written;
    }

    /* the ^Z  */
    do
    {
        written = write(s_fd, "\032", 1);
    }
    while ((written < 0 && errno == EINTR) || (written == 0));

    if (written < 0)
    {
        return AT_ERROR_GENERIC;
    }

    return 0;
}

/**
 * returns 0 on success, -1 on error
 */
int at_channel_init(ATUnsolHandler h1)
{
    int ret;
    pthread_t   tid;
    pthread_attr_t  attr;

    s_unsolHandler = h1;
    s_readerClosed = 0;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    ret = pthread_create(&s_tid_reader, &attr, readerLoop, &attr);
	pthread_attr_destroy(&attr);

    if (ret < 0)
    {
        perror("pthread_create");
        return -1;
    }

    return 0;
}

/**
 * Starts AT handler on stream "fd'
 * returns 0 on success, -1 on error
 */
static int at_channel_struct_open(struct channel_struct *chan, int channelID, int fd)
{
    pthread_mutex_lock(&chan->s_commandmutex);
    chan->id = channelID;
    chan->ttyFd = fd;
    chan->s_responsePrefix = NULL;
    chan->s_smsPDU = NULL;
    chan->sp_response = NULL;
    chan->ATBuffer[0] = '\0';
    chan->ATBufferCur = chan->ATBuffer;
	memset(chan->ATBuffer, 0, sizeof(chan->ATBuffer));//clear AT rcv buffer, while reading from the begining zouxiaojie 20140609
    chan->timeout_count = 0;
    pthread_mutex_unlock(&chan->s_commandmutex);
    return 0;
}

int at_channel_open(int channelID, int fd)
{
    struct channel_struct   *chan = &channels[channelID];
    return at_channel_struct_open(chan, channelID, fd);
}

/* FIXME is it ok to call this from the reader and the command thread? */
static void at_channel_struct_close(struct channel_struct *chan)
{
    if (chan->ttyFd >= 0)
    {
        close(chan->ttyFd);
    }

    chan->ttyFd = -1;
}

void at_channel_close(int channelID)
{
    at_channel_struct_close(&channels[channelID]);
}

static ATResponse * at_response_new()
{
    return (ATResponse *) calloc(1, sizeof(ATResponse));
}

/* It is called manually by user only in syn case. For asyn case it is auto released in handleFinalResponse() */
void at_response_free(ATResponse *p_response)
{
    ATLine  *p_line;

    if (p_response == NULL)
    {
        return;
    }

    p_line = p_response->p_intermediates;

    while (p_line != NULL)
    {
        ATLine  *p_toFree;

        p_toFree = p_line;
        p_line = p_line->p_next;

        free(p_toFree->line);
        free(p_toFree);
    }

    free(p_response->finalResponse);
    free(p_response);
}

/**
 * The line reader places the intermediate responses in reverse order
 * here we flip them back
 */
static void reverseIntermediates(ATResponse *p_response)
{
    ATLine  *pcur,
    *pnext;

    pcur = p_response->p_intermediates;
    p_response->p_intermediates = NULL;

    while (pcur != NULL)
    {
        pnext = pcur->p_next;
        pcur->p_next = p_response->p_intermediates;
        p_response->p_intermediates = pcur;
        pcur = pnext;
    }
}


static int at_send_command_full_nolock(struct channel_struct *chan, const char *command, ATCommandType type,
                                       const char *responsePrefix, const char *smspdu, long long timeoutMsec,
                                       ATResponse **pp_outResponse)
{
	//ALOGD(" ==== handle_creg <bufantong> /n");
    MYLOG("at_send_command_sync entry");
    int err = 0;
    if (chan->sp_response != NULL)
    {
        err = AT_ERROR_COMMAND_PENDING;
        ALOGE("%s: at error command pending", __FUNCTION__);
        goto error;
    }
	/*add by renyimin for 8 channels to 1 channel 2015 9 21 */
	#if ENABLE_ONECHANNEL
	//pthread_mutex_lock(&s_ttyFDMutex);
	#endif
		//ALOGD(" === 111 writeline bufantong\n");
    	err = writeline(chan, command);
		//ALOGD(" === 222 writeline bufantong\n");
	/*add by renyimin for 8 channels to 1 channel 2015 9 21 */
	#if ENABLE_ONECHANNEL
	//pthread_mutex_unlock(&s_ttyFDMutex);  
	#endif

	DO_ERROR_JDUGE;
    chan->s_type = type;
    chan->s_responsePrefix = responsePrefix;
    chan->s_smsPDU = smspdu;
    chan->sp_response = at_response_new();
    while (chan->sp_response->finalResponse == NULL && s_readerClosed == 0)
    {
        if (timeoutMsec != 0)
        {
            err = pthread_cond_timeout_np(&chan->s_commandcond, &chan->s_commandmutex, timeoutMsec);
        }
        else
        {
            err = pthread_cond_wait(&chan->s_commandcond, &chan->s_commandmutex);
        }
        if (err == ETIMEDOUT)
        {
            err = AT_ERROR_TIMEOUT;
            goto error;
        }
    }
    if (pp_outResponse == NULL)
    {
        at_response_free(chan->sp_response);
    }
    else
    {
        reverseIntermediates(chan->sp_response);/* line reader stores intermediate responses in reverse order */
        *pp_outResponse = chan->sp_response;
    }
    chan->sp_response = NULL;
    if (s_readerClosed > 0)
    {
        err = AT_ERROR_CHANNEL_CLOSED;
        goto error;
    }
    MYLOG("at_send_command_sync succ exit");
    return err;
    error : clearPendingCommand(chan);
    MYLOG("at_send_command_sync failure exit");
    return err;
}


int at_send_command_full_ps(int cid, const char *command, ATCommandType type, const char *responsePrefix, const char *smspdu,
                            long long timeoutMsec, ATResponse **pp_outResponse)
{
    ThreadContext   *context = getThreadContext();
    if (context == NULL || context->write_channel < 0)
    {
		ALOGE("not send at command in a queue thread:cid:%d;%s",cid,command);
        return AT_ERROR_INVALID_THREAD;
    }

    /*PS channel is gsmtty1/gsmtty2/gsmtty3/gsmtty4*/
    //int channelID = context->write_channel;
    int channelID = cid;

    //ALOGD("%s: channelID=%d", __FUNCTION__, channelID);

    struct channel_struct   *chan = &channels[channelID];
    if (chan->ttyFd < 0 || s_readerClosed > 0)
    {
        return AT_ERROR_CHANNEL_CLOSED;
    }

    if (timeoutMsec == 0)
    {
        timeoutMsec = context->channel_timeout_msec;
    }

    pthread_mutex_lock(&chan->s_commandmutex);
    int err = at_send_command_full_nolock(chan, command, type, responsePrefix, smspdu, timeoutMsec, pp_outResponse);
    pthread_mutex_unlock(&chan->s_commandmutex);

    if (err == AT_ERROR_TIMEOUT)
    {
        chan->timeout_count++;
        ALOGE("%s: at timeout, count = %d,channel = %d", __FUNCTION__, chan->timeout_count,chan->ttyFd);
		/* delete by RenYimin_2015-11-12 for yinfang project ,no need to check UART pin state BEGIN */
		//ALOGE("%s: ctsgpiostate = %d",__FUNCTION__,get_cts_gpio_state());
		/* delete by RenYimin_2015-11-12 for yinfang project ,no need to check UART pin state END */

    }
    return err;
}

int at_send_command_timeout_ps(int cid, const char *command, ATResponse **pp_outResponse, long long timeoutMsec)
{
    return at_send_command_full_ps(cid, command, NO_RESULT, NULL, NULL, timeoutMsec, pp_outResponse);
}



int at_send_command_full(const char *command, ATCommandType type, const char *responsePrefix, const char *smspdu,
                         long long timeoutMsec, ATResponse **pp_outResponse)
{
    ThreadContext   *context = getThreadContext();
    if (context == NULL || context->write_channel < 0)
    {
        ALOGE("not send at command in a queue thread:%s",command);
        return AT_ERROR_INVALID_THREAD;
    }

    int channelID = context->write_channel;
    struct channel_struct   *chan = &channels[channelID];
    if (chan->ttyFd < 0 || s_readerClosed > 0)
    {
        return AT_ERROR_CHANNEL_CLOSED;
    }

    if (timeoutMsec == 0)
    {
        timeoutMsec = context->channel_timeout_msec;
    }

    pthread_mutex_lock(&chan->s_commandmutex);
    int err = at_send_command_full_nolock(chan, command, type, responsePrefix, smspdu, timeoutMsec, pp_outResponse);
    pthread_mutex_unlock(&chan->s_commandmutex);

    if (err == AT_ERROR_TIMEOUT)
    {
        chan->timeout_count++;
        //ALOGE("%s: at timeout, count = %d", __FUNCTION__, chan->timeout_count);
        ALOGE("%s: at timeout, count = %d,channel = %d", __FUNCTION__, chan->timeout_count,chan->ttyFd);
		/* delete by RenYimin_2015-11-12 for yinfang project ,no need to check UART pin state BEGIN */
		//ALOGE("%s: ctsgpiostate = %d",__FUNCTION__,get_cts_gpio_state());
		/* delete by RenYimin_2015-11-12 for yinfang project ,no need to check UART pin state END */
	}
    return err;
}

int at_send_command_timeout(const char *command, ATResponse **pp_outResponse, long long timeoutMsec)
{
    return at_send_command_full(command, NO_RESULT, NULL, NULL, timeoutMsec, pp_outResponse);
}

int at_send_command_singleline_timeout(const char *command, const char *responsePrefix, ATResponse **pp_outResponse,
                                       long long timeoutMsec)
{
	//ALOGD(" ==== xx handle_creg <bufantong> /n");
    int err = at_send_command_full(command, SINGLELINE, responsePrefix, NULL, timeoutMsec, pp_outResponse);
    if (err == 0 &&
        pp_outResponse != NULL &&
        (*pp_outResponse) != NULL &&
        (*pp_outResponse)->success > 0 &&
        (*pp_outResponse)->p_intermediates == NULL)
    {
        /* Successful command must have an intermediate response. */
        at_response_free(*pp_outResponse);
        *pp_outResponse = NULL;
        err = AT_ERROR_INVALID_RESPONSE;
    }

    return err;
}

int at_send_command_numeric_timeout(const char *command, ATResponse **pp_outResponse, long long timeoutMsec)
{
    int err;

    err = at_send_command_full(command, NUMERIC, NULL, NULL, timeoutMsec, pp_outResponse);

    if (err == 0 &&
        pp_outResponse != NULL &&
        (*pp_outResponse) != NULL &&
        (*pp_outResponse)->success > 0 &&
        (*pp_outResponse)->p_intermediates == NULL)
    {
        /* Successful command must have an intermediate response. */
        at_response_free(*pp_outResponse);
        *pp_outResponse = NULL;
        err = AT_ERROR_INVALID_RESPONSE;
    }

    return err;
}
/*xichunyan 2012-07-16 add MULTILINE_SMS for getting pud by ZMGR begin */
int at_send_command_multiline_sms(const char *command, const char *responsePrefix, ATResponse **pp_outResponse,
                                  long long timeoutMsec)
{
    return at_send_command_full(command, MULTILINE_SMS, responsePrefix, NULL, timeoutMsec, pp_outResponse);
}
/*xichunyan 2012-07-16 add MULTILINE_SMS for getting pud by ZMGR end */

int at_send_command_multiline_timeout(const char *command, const char *responsePrefix, ATResponse **pp_outResponse,
                                      long long timeoutMsec)
{
    return at_send_command_full(command, MULTILINE, responsePrefix, NULL, timeoutMsec, pp_outResponse);
}

int at_send_command_sms_timeout(const char *command, const char *pdu, const char *responsePrefix, ATResponse **pp_outResponse,
                                long long timeoutMsec)
{
    int err;


    err = at_send_command_full(command, SINGLELINE, responsePrefix, pdu, timeoutMsec, pp_outResponse);

    if (err == 0 &&
        pp_outResponse != NULL &&
        (*pp_outResponse) != NULL &&
        (*pp_outResponse)->success > 0 &&
        (*pp_outResponse)->p_intermediates == NULL)
    {
        /* Successful command must have an intermediate response. */
        at_response_free(*pp_outResponse);
        *pp_outResponse = NULL;
        err = AT_ERROR_INVALID_RESPONSE;
    }

    return err;
}

//Johnny: need to consider when to call it?
/** This callback is invoked on the command thread */
void at_set_on_timeout(void (*onTimeout) (int channelID))
{
    s_onTimeout = onTimeout;
}

/**
 *  This callback is invoked on the reader thread (like ATUnsolHandler)
 *  when the input stream closes before you call at_close
 *  (not when you call at_close())
 *  You should still call at_close()
 */
void at_set_on_reader_closed(void (*onClose) (void))
{
    s_onReaderClosed = onClose;
}

/**
 * Returns error code from response
 * Assumes AT+CMEE=1 (numeric) mode
 */
int at_get_error(const ATResponse *p_response, const char *prefix)
{
    int ret;
    int err;
    char    *p_cur;

    if (p_response->success > 0)
    {
        return 0;
    }

    if (p_response->finalResponse == NULL || !strStartsWith(p_response->finalResponse, prefix))
    {
        return -1;
    }

    p_cur = p_response->finalResponse;
    err = at_tok_start(&p_cur);
    if (err < 0)
    {
        return -1;
    }

    err = at_tok_nextint(&p_cur, &ret);
    if (err < 0)
    {
        return -1;
    }

    return ret;
}

AT_CME_Error at_get_cme_error(const ATResponse *p_response)
{
    return (AT_CME_Error) at_get_error(p_response, "+CME ERROR:");
}

AT_CMS_Error at_get_cms_error(const ATResponse *p_response)
{
    return (AT_CMS_Error) at_get_error(p_response, "+CMS ERROR:");
}

#define DO_AT_CHANNEL_ERROR_RETURN \
    do{\
        if (err < 0)  return -1;\
    }while(0)

int at_switch_data_mode(struct psd_channel_decription *pd, const char *cmd, ATResponse **pp_outResponse)
{
    int err = -1, fd = -1;
    const char  *line;
    struct channel_struct   channel;
    fd = open(pd->ttyName, O_RDWR);
    if (fd >= 0)
    {
        pd->fd = fd;
    }
    else
    {
        ALOGE("Open data channle %s error:%s!!!!!\n", pd->ttyName, strerror(errno));
        return fd;
    }
    ALOGI("enter at_switch_data_mode_ppp:open tty_name:%s,channle ID is %d\n", pd->ttyName, pd->channelID);
    struct channel_struct   *chan = &channel;
    init_channel_struct(chan);
    at_channel_struct_open(chan, pd->channelID, fd);
    err = writeline(chan, cmd);
	DO_AT_CHANNEL_ERROR_RETURN;
    chan->sp_response = at_response_new();
    *pp_outResponse = chan->sp_response;
    err = -1;
    while (err != 0)
    {
        readChannel(chan);
        while (1)
        {
            line = get_at_buffer_line(chan);
            ALOGI("AT channel [%d] receive: %s", chan->id, line);
            if (line == NULL)
            {
                err = -1;
                break;
            }
            else if (isFinalResponseSuccess(line))
            {
                chan->sp_response->success = 1;
                err = 0;
                break;
            }
            else if (isFinalResponseError(line))
            {
                chan->sp_response->success = 0;
                err = 0;
                break;
            }
        }
    }
    at_channel_struct_close(chan);
    return err;
}
static int get_cts_gpio_state()
{
	int fd_cts = 0;
	char cts_gpio_state[1];
	int ret = 0;

	fd_cts = open("/sys/bus/platform/drivers/comip-uart/ctsgpiovalue", O_NONBLOCK|O_RDONLY); 
	if (fd_cts <= 0)
	{
		ALOGE("Error opening device: /sys/bus/platform/drivers/comip-uart/ctsgpiovalue\n");
		return -1;
	}
	ret = read(fd_cts,	cts_gpio_state, 1);
	close(fd_cts);
	if (ret <= 0)
	{
		ALOGE("Error read device: /sys/bus/platform/drivers/comip-uart/ctsgpiovalue\n");
		return -1;
	}
	if (cts_gpio_state[0] == '1')
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

#ifdef TTY_USB_CHECK_AND_RESET
//20160719
void at_close_fds()
{
#if 0
         int m =0;
    int ret = 0;
 
         for(m = 0; m < MAX_COUNT; m++)
         {
             if(sp_request_param[m].m_fd > 0)
                   {
                   ret = close(sp_request_param[m].m_fd);
                   sp_request_param[m].m_fd = 0;
            RLOGD("at_close_fds close sp_request_param[%d].m_fd ret = %d", m, ret);
             }
         }
#endif
         onReaderClosed();
}


int at_check_dev_interface()
{
    DIR *d;
    struct dirent *de;
    int ret_val = -1;
    int exit_count = 0;
 
    if (NULL == (d = opendir("/dev")))
    {
//        RLOGE("[at_check_dev_interface] open failed");
        goto exit;
    }
 
    while(de = readdir(d))
    {
        if('.' == de->d_name[0])
            continue;
        if(3 == exit_count)
        {
            //DBBD(DB_CMD, RLOGI("[at_check_dev_interface] check OK"));
        #ifdef TTY_USB_CHECK_DEBUG  //added by Gelei,for debug,20161209
           	ALOGE("[at_check_dev_interface] check OK");                              //20160928
		#endif
			ret_val = 0;
            goto exit;
        }
        if(!strncmp(de->d_name,"ttyUSB0",strlen("ttyUSB0")))
        {
            //DBBD(DB_CMD, RLOGI("[at_check_dev_interface] find ttyUSB0"));
		#ifdef TTY_USB_CHECK_DEBUG
			ALOGE("[at_check_dev_interface] find ttyUSB0\n");   //20160928
		#endif
			exit_count++;
        }
        if(!strncmp(de->d_name,"ttyUSB1",strlen("ttyUSB1")))
        {
            //DBBD(DB_CMD, RLOGI("[at_check_dev_interface] find ttyUSB1"));
		#ifdef TTY_USB_CHECK_DEBUG
			ALOGE("[at_check_dev_interface] find ttyUSB1\n");                           //20160928
		#endif
			exit_count++;
        }
        if(!strncmp(de->d_name,"ttyUSB2",strlen("ttyUSB2")))
        {
            //DBBD(DB_CMD, RLOGI("[at_check_dev_interface] find ttyUSB2"));
		#ifdef TTY_USB_CHECK_DEBUG
			ALOGE("[at_check_dev_interface] find ttyUSB2\n");                             //20160928          
		#endif
			exit_count++;
        }
    }
    RLOGE("[at_check_dev_interface] check failed");
 
exit:
    (NULL==d)?NULL:closedir(d);
    return ret_val;
}

////////////=====================================
#endif


