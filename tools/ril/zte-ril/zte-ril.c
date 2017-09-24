/******************************************************************************
 * *(C) Copyright 2012 ZTE International Ltd.
 * * All Rights Reserved
 * *   EC:		000000000000		
 * *       Reason: indicate sim status change for CP sim init again
 * *       Date	20140606 zouxiaojie
 * *   EC:		000000000000		
 * *       Reason: do not indicate sim status change while start, reg start property
 * *       Date	20140619 zouxiaojie
 * ******************************************************************************/

#include <assert.h>
#include <cutils/properties.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

#include <sys/socket.h>
#include <linux/netlink.h>
#include <termios.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cutils/properties.h>
#include <errno.h>


#include "at_tok.h"
#include "atchannel.h"
#include "zte-ril.h"
#include "misc.h"
#include "work-queue.h"
#include "ril-requestdatahandler.h"

#ifdef TTY_USB_CHECK_AND_RESET
#include <dirent.h>
#include <limits.h>


//20160719
#define RESET_PROCESS_SIGNAL 10
static pthread_t s_tid_ata;
extern int at_check_dev_interface();
//=========================
static void find_available_dev()
{
    DIR *d;
    struct dirent *de;
    int fd = 0;
    int ret = -1;
 
    if (NULL == (d = opendir("/dev")))
    {
        goto exit;
    }
 
    while(de = readdir(d))
    {
        if('.' == de->d_name[0])
            continue;
        if(strstr(de->d_name,"ttyUSB") != NULL)
        {
            char path[20] = {0};
           
            RLOGI("[find_available_dev] find %s", de->d_name);
            strcat(path, "/dev/");
            strcat(path, de->d_name);
            RLOGI("[find_available_dev] open %s", path);
            fd = open(path, O_RDWR);
            if(fd > 0)
            {
                //setTtyattr(fd);
                /*ret = write(fd , "AT^DSRST\r\n", strlen("AT^DSRST\r\n"));
                if(ret < 0)
                {
                    RLOGE("Write fd Error %s", strerror(errno));
                }*/
                break;
            }
            else
            {
                RLOGD("[find_available_dev] open error %s", strerror(errno));
            }
        }
    }
 
exit:
    (NULL==d)?NULL:closedir(d);
}

//
static void *check_dev_loop(void *arg)
{
    while(1)
    {
        if(0 == at_check_dev_interface())
        {   //gelei 20161209,for new check ttyUSB method. Twice.
#ifdef ZXW_RESET_METHOD
			usleep(200000);

#elif defined (ALLWINNER_REF_MACHINE_METHOD)
			usleep(200000);

#elif defined (RK_OTHERS_METHOD)
			usleep(200000);
#else 
		    usleep(200000);
#endif
		}
		else 
		{
			sleep(1);
			if(0 == at_check_dev_interface())
			{	//gelei 20161209
#ifdef ZXW_RESET_METHOD  //added  by boris
				 usleep(200000);
			 
#elif defined (ALLWINNER_REF_MACHINE_METHOD)
				 usleep(200000);
			 
#elif defined (RK_OTHERS_METHOD)
				 usleep(200000);
#else 
				 usleep(200000);
#endif
			}		
	        else
	        {
#ifdef ZXW_RESET_METHOD  //added by Boris,20161205  //gelei20161209
				//RLOGD("check_dev_loop ignore ttyUSB0 lost");
				RLOGD("ZXW check_dev_loop reset modem and ril"); 
			    system("am broadcast -a com.szchoiceway.eventcenter.EventUtils.ZXW_4G_SIGNAL_RESET_EVT --ei com.szchoiceway.eventcenter.EventUtils.ZXW_4G_SIGNAL_RESET_EVT_TRA 0");                  //ZXW GELEI  20161104
			   	sleep(2);     
			    system("am broadcast -a com.szchoiceway.eventcenter.EventUtils.ZXW_4G_SIGNAL_RESET_EVT --ei com.szchoiceway.eventcenter.EventUtils.ZXW_4G_SIGNAL_RESET_EVT_TRA 1");                     //ZXW  GELEI   20161104
				#endif

#ifdef ALLWINNER_REF_MACHINE_METHOD//gelei 20161209
				RLOGD("ALLWINNER check_dev_loop reset modem and ril");
				system("echo 0 > /sys/class/lc4g_ctrl/lc4g_ctrl/enable");//bufantong 20160829
				sleep(2);
			    system("echo 1 > /sys/class/lc4g_ctrl/lc4g_ctrl/enable");
#endif

#ifdef RK_OTHERS_METHOD//gelei 20161209
				RLOGD("RK check_dev_loop reset modem and ril");
#endif
			 
			   	RLOGD("check_dev_loop_reset MODULE");
			    at_close_fds();
			    find_available_dev();
			    kill(getpid(), RESET_PROCESS_SIGNAL);
			    return NULL;
	        }
        }
		//usleep(20000);
    }
    return NULL;
}
#if 0
{
        int ret = 0;
        pthread_t tid;
        pthread_attr_t attr;
 
        RLOGD( "RIL_Init check dev thread");
        pthread_attr_init (&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        ret = pthread_create(&s_tid_ata, &attr, check_dev_loop, &attr);
        if (ret < 0) {
            RLOGD( "RIL_Init check dev thread fail");
            perror ("pthread_create");
        }
}
#endif
//===============================================================================

#endif


static struct termios   oldtio,newtio;

struct gsm_config
{
    unsigned int    adaption;
    unsigned int    encapsulation;
    unsigned int    initiator;
    unsigned int    t1;
    unsigned int    t2;
    unsigned int    t3;
    unsigned int    n2;
    unsigned int    mru;
    unsigned int    mtu;
    unsigned int    k;
    unsigned int    i;
    unsigned int    unused[8];     /* Padding for expansion without  breaking stuff */
};

#define GSMIOC_GETCONF      _IOR('G', 0, struct gsm_config)
#define GSMIOC_SETCONF      _IOW('G', 1, struct gsm_config)
#define N_GSM0710 21

static int  baudrate = 921600;
int dtmf_end = 0;

char    *ttydev = UART_TTY;
#define GSMTTY_MAX 13

#define MAX_AT_RESPONSE_LENGTH      64



static struct gsm_config    n_gsmcfg =
{
    .adaption = 0,
    .encapsulation = 0,
    .initiator = 1,
    .t1 = 0,
    .t2 = 0,
    .t3 = 0,
    .n2 = 0,
    .mru = 1500,
    .mtu = 1500,
    .k = 0,
    .i = 0,

};

static int  gsmtty_fd[GSMTTY_MAX] =
{
    -1
};


/* added by RenYimin_2015-11-12 for AP and CP communication BEGIN */
// ttydev_fd for one channel in yinfang project
static int  ttydev_fd = -1;

#define TTY_DEV_NAME "/dev/ttyUSB0"

pthread_cond_t  s_start_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t s_start_mutex = PTHREAD_MUTEX_INITIALIZER;

/* added by RenYimin_2015-11-12 for AP and CP communication END */
		     

static int  serial_fd = -1;

typedef struct
{
    int baud_rate;
    int termios_value;
} tBaudRates;

tBaudRates  baud_rates[] =
{
    { 57600,  B57600 },
    { 115200, B115200 },
    { 230400, B230400 },
    { 460800, B460800 },
    { 500000, B500000 },
    { 576000, B576000 },
    { 921600, B921600 },
    { 1000000, B1000000 },
    { 1152000, B1152000 },
    { 1500000, B1500000 },
    { 2000000, B2000000 },
    { 2500000, B2500000 },
    { 3000000, B3000000 },
    { 3500000, B3500000 },
    { 4000000, B4000000 }
};


#define ZTERILD_MODEM_STANDBYMODE_PROPERTY  "persist.radio.standbymode"

static pthread_key_t    sKey;

/* Global varialble used in ril layer */
RIL_RadioState  sState = RADIO_STATE_UNAVAILABLE;

pthread_mutex_t s_state_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  s_state_cond = PTHREAD_COND_INITIALIZER;

int s_closed = 0;  /* trigger change to this with s_state_cond */
extern int  AfterCPReset;
extern int correctStandmode(void);
void setNetworkStateReportOption(int flag);
int getSimStatus(void);
void updateRadioState(void);
void activeUsimforTsp(void);

/* static I/F which will be called by Android upper layer */
static void onRequest(int request, void *data, size_t datalen, RIL_Token token);
static RIL_RadioState onCurrentState();
static int onSupports(int requestCode);
static void onCancel(RIL_Token token);
static const char *onGetVersion();

/* Internal function declaration */
static void onUnsolicited(const char *s, const char *smsPdu);

/*** Static Variables ***/
static const RIL_RadioFunctions s_callbacks =
{
    RIL_VERSION,
    onRequest,
    onCurrentState,
    onSupports,
    onCancel,
    onGetVersion
};

#define DATABITS       CS8

static int  modemReady = 0 ;
static int  iccReady = 0 ;


static void handle_icc(const char *s, const char *smsPdu);
static void handle_zmsri(const char *s, const char *smsPdu);


#define BAUD          B921600
#define STOPBITS        0
#define PARITYON        0
#define PARITY            0

/* 通道描述符 */
struct channel_description  *descriptions;
int *service_channel_map;
struct psd_channel_decription   ppp_channel ={0};
static int  channel_count;

int modem_mode ;
Modem_Type  modemType = TSP_CP;
extern int  bNeedDownload;

//#ifdef TSP_CP
/* modified by RenYimin_2015-11-12 for 8 channel to 1 channel BEGIN */
#define  ENABLE_ONECHANNEL 1



struct channel_description  dcp_descriptions[] =
{
    [CHANNEL_00] = {    "/dev/gsmtty16",     "RIL-UNSOL",    NULL },
#if ENABLE_ONECHANNEL
    /*delete by renyimin for 8 channels to 1 channel 2015 9 18 */
/*
    [CHANNEL_01] = {    "/dev/gsmtty17",     "RIL-DEV",  NULL },
    [CHANNEL_02] = {    "/dev/gsmtty18",     "RIL-MM",  NULL },
    [CHANNEL_03] = {    "/dev/gsmtty19",     "RIL-MSG", NULL },
    [CHANNEL_04] = {    "/dev/gsmtty20",     "RIL-PS",  NULL },
    [CHANNEL_05] = {    "/dev/gsmtty21",     "RIL-SIM",  NULL },
    [CHANNEL_06] = {    "/dev/gsmtty22",     "RIL-SS",   NULL },
    [CHANNEL_07] = {    "/dev/gsmtty23",     "RIL-CC",   NULL },
    */
    
#else
    [CHANNEL_01] = {    "/dev/gsmtty17",     "RIL-DEV",  NULL },
    [CHANNEL_02] = {    "/dev/gsmtty18",     "RIL-MM",  NULL },
    [CHANNEL_03] = {    "/dev/gsmtty19",     "RIL-MSG", NULL },
    [CHANNEL_04] = {    "/dev/gsmtty20",     "RIL-PS",  NULL },
    [CHANNEL_05] = {    "/dev/gsmtty21",     "RIL-SIM",  NULL },
    [CHANNEL_06] = {    "/dev/gsmtty22",     "RIL-SS",   NULL },
    [CHANNEL_07] = {    "/dev/gsmtty23",     "RIL-CC",   NULL },

#endif

};

/* 通道描述符 */
struct channel_description  scp_descriptions[] =
{
    [CHANNEL_00] = {    "/dev/gsmtty1",     "RIL-UNSOL",    NULL },
#if ENABLE_ONECHANNEL
/*delete by renyimin for 8 channels to 1 channel 2015 9 18 */
/*
    [CHANNEL_01] = {    "/dev/gsmtty2",     "RIL-DEV",  NULL },
    [CHANNEL_02] = {    "/dev/gsmtty3",     "RIL-MM",  NULL },
    [CHANNEL_03] = {    "/dev/gsmtty4",     "RIL-MSG", NULL },
    [CHANNEL_04] = {    "/dev/gsmtty5",     "RIL-PS",  NULL },
    [CHANNEL_05] = {    "/dev/gsmtty6",     "RIL-SIM",  NULL },
    [CHANNEL_06] = {    "/dev/gsmtty7",     "RIL-SS",   NULL },
    [CHANNEL_07] = {    "/dev/gsmtty8",     "RIL-CC",   NULL },
*/
#else
    [CHANNEL_01] = {    "/dev/gsmtty2",     "RIL-DEV",  NULL },
    [CHANNEL_02] = {    "/dev/gsmtty3",     "RIL-MM",  NULL },
    [CHANNEL_03] = {    "/dev/gsmtty4",     "RIL-MSG", NULL },
    [CHANNEL_04] = {    "/dev/gsmtty5",     "RIL-PS",  NULL },
    [CHANNEL_05] = {    "/dev/gsmtty6",     "RIL-SIM",  NULL },
    [CHANNEL_06] = {    "/dev/gsmtty7",     "RIL-SS",   NULL },
    [CHANNEL_07] = {    "/dev/gsmtty8",     "RIL-CC",   NULL },

#endif
};

/* modified by RenYimin_2015-11-12 for 8 channel to 1 channel  END */

int cp_service_channel_map[] =
{
    [SERVICE_UNSOL] = CHANNEL_00,
    [SERVICE_DEV] = CHANNEL_01,
    [SERVICE_MM] = CHANNEL_02,
    [SERVICE_MSG] = CHANNEL_03,
    [SERVICE_PS] = CHANNEL_04,
    [SERVICE_SIM] = CHANNEL_05,
    [SERVICE_SS] = CHANNEL_06,
    [SERVICE_CC] = CHANNEL_07,

};

/*the ppp devices will be check in debug with TSP modem*/
struct psd_channel_decription   cp_ppp_channel =
{
    0,
    0,
    "/dev/cidatatty1",
    "/var/lock/LCK..cidatatty1"
};


static int rilhostonModem(void)
{
    int fd = -1;
    char value = 0;

    fd = open("/sys/devices/platform/modem_control/hoston", 2);
    if (fd < 0)
    {
        return -1;
    }

    if (read(fd, &value, 1) != 1)
    {
        close(fd);
        return -2;
    }
	
    if (write(fd, "0", 2) < 0)
    {
    	ALOGD("hostonModem write 0 error \n");
		close(fd);
    	return -3;
    }
	
    sleep(3);
	
    if (write(fd, "1", 2) < 0)
    {
       	ALOGD("hostonModem write 1 error \n");
		close(fd);
    	return -4;
    }
	ALOGD("hostonModem reset success \n");
    close(fd);
    return 0;
}


static void send_at_zsrvans(void *arg)
{ 
    int err;
    err = at_send_command("AT+ZSRVANS=1",NULL);
    if (err < 0)
    {
     ALOGD("handle_zsrving : err = %d",err);
    }
    return;
}

/* added by RenYimin_2015-11-12 for  incoming call in 4G network BEGIN */
void handle_zsrving(const char *s, const char *smsPdu)
{
     UNUSED(s);
     UNUSED(smsPdu);
     enque(getWorkQueue(SERVICE_CC), send_at_zsrvans, NULL);
}
/* added by RenYimin_2015-11-12 for  incoming call in 4G network END */




static void handle_icc(const char *s, const char *smsPdu)
{
    UNUSED(smsPdu);
    char    *line = NULL,
    *linesave = NULL;
    int err;
    char *result, iccid[21];
    iccReady = 1;
	bNeedDownload = 1;
	  
	line = strdup(s);
    linesave = line;
    err = at_tok_start(&line);
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextstr(&line, &result);
    if (err < 0)
    {
        goto error;
    }
      // sleep(5);   //鍙栨秷鐫?绉掑姛鑳?   gelei  20161021  
    sprintf(iccid, "%s", result);
    ALOGD(" iccid = %s",iccid);
	property_set("ril.iccid", iccid);
    RIL_onUnsolicitedResponse(RIL_UNSOL_SIM_ICCID, iccid, strlen(iccid));

	 if (linesave != NULL) free(linesave);
	 return;

error:
	if (linesave != NULL) free(linesave);
	ALOGE("%s: Error parameter in ind msg: %s", __FUNCTION__, s);
	return;
}


static void handle_zmsri(const char *s, const char *smsPdu)
{
    ALOGI("handle_zmsri: set modemReady=1 ");
    modemReady = 1;
}

/* 设置通道 */
void set_channles(Modem_Type modemType, int modem_mode)
{
    struct channel_description  *cp_descriptions = scp_descriptions;
    if (modemType == TSP_CP)
    {
    	
        descriptions = cp_descriptions;
        service_channel_map = cp_service_channel_map;
        memcpy(&ppp_channel, &cp_ppp_channel, sizeof(struct psd_channel_decription));
        channel_count = (sizeof(scp_descriptions) / sizeof(scp_descriptions[0]));
	 ALOGD("set_channles  channel_count = %d ", channel_count);
    }

    return;
}

/*
void set_channles(Modem_Type modemType, int modem_mode)
{
    if (modemType == TSP_CP)
    {   
        if(standbymode() == 1)
        {
        	descriptions = dcp_descriptions;
        }
		else {
				descriptions = scp_descriptions;
		}
		     service_channel_map = cp_service_channel_map;
        memcpy(&ppp_channel, &cp_ppp_channel, sizeof(struct psd_channel_decription));
        channel_count = (sizeof(dcp_descriptions) / sizeof(dcp_descriptions[0]));
    }

    return;
}

*/




int get_channel_number(void)
{
    //return (sizeof(descriptions)/sizeof(descriptions[0]));
    ALOGD("get_channel_number = %d ", channel_count);
	/* 通道的数量，在set_channles函数中设置的 */
    return channel_count;
}

static const struct service_info    s_services[] =
{
    {SERVICE_UNSOL,     NULL,   NULL},
    {SERVICE_PS,        NULL,   NULL},
    {SERVICE_VT,        NULL,   NULL},
    {SERVICE_CC,        cc_pre_process_request, cc_post_process_request},
    {SERVICE_MM,        NULL,   NULL},
    {SERVICE_MSG,       NULL,   NULL},
    {SERVICE_SIM,       NULL,   NULL},
    {SERVICE_DEV_SS,    NULL,   NULL},
    {SERVICE_TOTAL,     NULL,   NULL},

};

/* 创建工作队列 */
static void createWorkQueues()
{
    int i;
    int n = get_channel_number();
    for (i = 0; i < n; i++)
    {
        if (descriptions[i].workQueueName)
        {
        	ALOGI("descriptions[%d].workQueueName =  %s \r\n",i,descriptions[i].workQueueName);
            descriptions[i].workQueue = createWorkQueue(descriptions[i].workQueueName, i, TIMEOUT_DEFALUT);
        }
    }
}

struct WorkQueue * getWorkQueue(int service)
{
	assert(service < SERVICE_UNSOL);
/* modified by RenYimin_2015-11-12 for 8 channel to 1 channel  BEGIN */
	//from service_channel_map[service] to 0
	
#if ENABLE_ONECHANNEL 
return descriptions[0].workQueue;
#else 
return descriptions[service_channel_map[service]].workQueue;
	

#endif
	
/* modified by RenYimin_2015-11-12 for 8 channel to 1 channel  END */	

	

}


void setThreadContext(ThreadContext *context)
{
	/* 设置线程私有数据 */
    pthread_setspecific(sKey, context);
}
ThreadContext * getThreadContext()
{
    return (ThreadContext *) pthread_getspecific(sKey);
}

const char * getLogTag()
{
    ThreadContext   *context = getThreadContext();
    return context ? context->threadname : "RIL";
}

/*************************************************************
 *  request handler definition
 **************************************************************/


static const struct request_info    s_requests[] =
{
    {0, SERVICE_NULL, NULL},
    {RIL_REQUEST_GET_SIM_STATUS,                             SERVICE_SIM,    ril_request_get_sim_status},
    {RIL_REQUEST_ENTER_SIM_PIN,                              SERVICE_SIM,    ril_handle_sim_pin_puk},
    {RIL_REQUEST_ENTER_SIM_PUK,                              SERVICE_SIM,    ril_handle_sim_pin_puk},
    {RIL_REQUEST_ENTER_SIM_PIN2,                             SERVICE_SIM,    ril_handle_sim_pin_puk},
    {RIL_REQUEST_ENTER_SIM_PUK2,                             SERVICE_SIM,    ril_handle_sim_pin_puk},
    {RIL_REQUEST_CHANGE_SIM_PIN,                             SERVICE_SIM,    ril_handle_sim_pin_puk},
    {RIL_REQUEST_CHANGE_SIM_PIN2,                            SERVICE_SIM,    ril_handle_sim_pin_puk},
    {RIL_REQUEST_ENTER_NETWORK_DEPERSONALIZATION,            SERVICE_SIM,    ril_handle_sim_pin_puk},
    {RIL_REQUEST_GET_CURRENT_CALLS,                          SERVICE_CC,     ril_request_get_current_calls},
    {RIL_REQUEST_DIAL,                                       SERVICE_CC,     ril_request_dial},
    {RIL_REQUEST_GET_IMSI,                                   SERVICE_SIM,    ril_request_get_imsi},
    {RIL_REQUEST_HANGUP,                                     SERVICE_CC,     ril_request_hangup},
    {RIL_REQUEST_HANGUP_WAITING_OR_BACKGROUND,               SERVICE_CC,     ril_request_hangup_waiting_or_background},
    {RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND,        SERVICE_CC,     ril_request_hangup_foreground_resume_background},
    {RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE,       SERVICE_CC,     ril_request_switch_waiting_or_holding_and_active},
    {RIL_REQUEST_CONFERENCE,                                 SERVICE_CC,     ril_request_conference},
    {RIL_REQUEST_UDUB,                                       SERVICE_CC,     ril_request_udub},
    {RIL_REQUEST_LAST_CALL_FAIL_CAUSE,                       SERVICE_CC,     ril_request_last_call_fail_cause},
    {RIL_REQUEST_SIGNAL_STRENGTH,                            SERVICE_MM,     ril_request_signal_strength},
    {RIL_REQUEST_VOICE_REGISTRATION_STATE,                   SERVICE_MM,     ril_request_voice_registration_state},
    {RIL_REQUEST_DATA_REGISTRATION_STATE,                    SERVICE_MM,     ril_request_data_registration_state},
    {RIL_REQUEST_OPERATOR,                                   SERVICE_MM,     ril_request_operator},
    {RIL_REQUEST_RADIO_POWER,                                SERVICE_MM,     ril_request_radio_power},
    {RIL_REQUEST_DTMF,                                       SERVICE_CC,     ril_request_dtmf},
    {RIL_REQUEST_SEND_SMS,                                   SERVICE_MSG,    ril_request_send_sms},
    {RIL_REQUEST_SEND_SMS_EXPECT_MORE,                       SERVICE_MSG,    ril_request_send_sms},

	{RIL_REQUEST_SETUP_DATA_CALL,                            SERVICE_PS,     ril_request_setup_data_call},
    {RIL_REQUEST_SIM_IO,                                     SERVICE_SIM,    ril_request_sim_io},
    {RIL_REQUEST_SEND_USSD,                                  SERVICE_SS,     ril_request_send_ussd},
    {RIL_REQUEST_CANCEL_USSD,                                SERVICE_SS,     ril_request_cancel_ussd},
    {RIL_REQUEST_GET_CLIR,                                   SERVICE_SS,     ril_request_get_clir},
    {RIL_REQUEST_SET_CLIR,                                   SERVICE_SS,     ril_request_set_clir},
    {RIL_REQUEST_QUERY_CALL_FORWARD_STATUS,                  SERVICE_SS,     ril_request_query_call_forward_status},
    {RIL_REQUEST_SET_CALL_FORWARD,                           SERVICE_SS,     ril_request_set_call_forward},
    {RIL_REQUEST_QUERY_CALL_WAITING,                         SERVICE_SS,     ril_request_query_call_waiting},
    {RIL_REQUEST_SET_CALL_WAITING,                           SERVICE_SS,     ril_request_set_call_waiting},
    {RIL_REQUEST_SMS_ACKNOWLEDGE,                            SERVICE_MSG,    ril_request_sms_acknowledge},
    {RIL_REQUEST_GET_IMEI,                                   SERVICE_DEV,    ril_request_get_imei},
    {RIL_REQUEST_GET_IMEISV,                                 SERVICE_DEV,    ril_request_get_imeisv},
    {RIL_REQUEST_ANSWER,                                     SERVICE_CC,     ril_request_answer},

	{RIL_REQUEST_DEACTIVATE_DATA_CALL,                       SERVICE_PS,    ril_request_deactivate_data_call},
    {RIL_REQUEST_QUERY_FACILITY_LOCK,                        SERVICE_SS,     ril_request_query_facility_lock},
    {RIL_REQUEST_SET_FACILITY_LOCK,                          SERVICE_SIM,    ril_handle_sim_pin_puk},
    {RIL_REQUEST_CHANGE_BARRING_PASSWORD,                    SERVICE_SS,     ril_request_change_barring_password},
    {RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE,               SERVICE_MM,     ril_request_query_network_selection_mode},
    {RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC,            SERVICE_MM,     ril_request_set_network_selection_automatic},
    {RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL,               SERVICE_MM,     ril_request_set_network_selection_manual},
    {RIL_REQUEST_QUERY_AVAILABLE_NETWORKS,                   SERVICE_MM,     ril_request_query_available_networks},
    {RIL_REQUEST_DTMF_START,                                 SERVICE_CC,     ril_request_dtmf_start},
    {RIL_REQUEST_DTMF_STOP,                                  SERVICE_CC,     ril_request_dtmf_stop},
    {RIL_REQUEST_BASEBAND_VERSION,                           SERVICE_DEV,    ril_request_baseband_version},
    {RIL_REQUEST_SEPARATE_CONNECTION,                        SERVICE_CC,     ril_request_separate_connection},
    {RIL_REQUEST_SET_MUTE,                                   SERVICE_CC,     ril_request_set_mute},
    {RIL_REQUEST_GET_MUTE,                                   SERVICE_CC,     ril_request_get_mute},
    {RIL_REQUEST_QUERY_CLIP,                                 SERVICE_SS,     ril_request_query_clip},
    {RIL_REQUEST_LAST_DATA_CALL_FAIL_CAUSE,                  SERVICE_PS,     NULL},
    {RIL_REQUEST_DATA_CALL_LIST,                             SERVICE_PS,     ril_request_data_call_list},
    {RIL_REQUEST_RESET_RADIO,                                SERVICE_MM,     ril_request_radio_power_hardreset},
    {RIL_REQUEST_OEM_HOOK_RAW,                               SERVICE_DEV,    ril_request_oem_hook_raw},
    {RIL_REQUEST_OEM_HOOK_STRINGS,                           SERVICE_DEV,    ril_request_oem_hook_strings},
    {RIL_REQUEST_SCREEN_STATE,                               SERVICE_MM,     ril_request_screen_state},
    {RIL_REQUEST_SET_SUPP_SVC_NOTIFICATION,                  SERVICE_SS,     ril_request_set_supp_svc_notification},
    {RIL_REQUEST_WRITE_SMS_TO_SIM,                           SERVICE_MSG,    ril_request_write_sms_to_sim},
    {RIL_REQUEST_DELETE_SMS_ON_SIM,                          SERVICE_MSG,    ril_request_delete_sms_on_sim},
    {RIL_REQUEST_SET_BAND_MODE,                              SERVICE_DEV,    ril_request_set_band_mode},
    {RIL_REQUEST_QUERY_AVAILABLE_BAND_MODE,                  SERVICE_DEV,    ril_request_query_available_band_mode},
    {RIL_REQUEST_STK_GET_PROFILE,                            SERVICE_SIM,    ril_handle_stk_cmd_tsp},
    {RIL_REQUEST_STK_SET_PROFILE,                            SERVICE_SIM,    ril_handle_stk_cmd_tsp},
    {RIL_REQUEST_STK_SEND_ENVELOPE_COMMAND,                  SERVICE_SIM,    ril_handle_stk_cmd_tsp},
    {RIL_REQUEST_STK_SEND_TERMINAL_RESPONSE,                 SERVICE_SIM,    ril_handle_stk_cmd_tsp},
    {RIL_REQUEST_STK_HANDLE_CALL_SETUP_REQUESTED_FROM_SIM,   SERVICE_SIM,    ril_handle_stk_cmd_tsp},
    {RIL_REQUEST_EXPLICIT_CALL_TRANSFER,                     SERVICE_CC,     ril_request_explicit_call_transfer},
    {RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE,                 SERVICE_MM,     ril_request_set_preferred_network_type},
    {RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE,                 SERVICE_MM,     ril_request_get_preferred_network_type},
    {RIL_REQUEST_GET_NEIGHBORING_CELL_IDS,                   SERVICE_MM,     ril_request_get_neighboring_cell_ids},
    {RIL_REQUEST_SET_LOCATION_UPDATES,                       SERVICE_MM,     ril_request_set_location_updates},
    {RIL_REQUEST_CDMA_SET_SUBSCRIPTION_SOURCE,               SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_SET_ROAMING_PREFERENCE,                SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_QUERY_ROAMING_PREFERENCE,              SERVICE_NULL,   NULL},
    {RIL_REQUEST_SET_TTY_MODE,                               SERVICE_NULL,   NULL},
    {RIL_REQUEST_QUERY_TTY_MODE,                             SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_SET_PREFERRED_VOICE_PRIVACY_MODE,      SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_QUERY_PREFERRED_VOICE_PRIVACY_MODE,    SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_FLASH,                                 SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_BURST_DTMF,                            SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_VALIDATE_AND_WRITE_AKEY,               SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_SEND_SMS,                              SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_SMS_ACKNOWLEDGE,                       SERVICE_NULL,   NULL},
    {RIL_REQUEST_GSM_GET_BROADCAST_SMS_CONFIG,               SERVICE_MSG,    ril_request_gsm_get_broadcast_sms_config},
    {RIL_REQUEST_GSM_SET_BROADCAST_SMS_CONFIG,               SERVICE_MSG,    ril_request_gsm_set_broadcast_sms_config},
    {RIL_REQUEST_GSM_SMS_BROADCAST_ACTIVATION,               SERVICE_MSG,    ril_request_gsm_sms_broadcast_activation},
    {RIL_REQUEST_CDMA_GET_BROADCAST_SMS_CONFIG,              SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_SET_BROADCAST_SMS_CONFIG,              SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_SMS_BROADCAST_ACTIVATION,              SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_SUBSCRIPTION,                          SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_WRITE_SMS_TO_RUIM,                     SERVICE_NULL,   NULL},
    {RIL_REQUEST_CDMA_DELETE_SMS_ON_RUIM,                    SERVICE_NULL,   NULL},
    {RIL_REQUEST_DEVICE_IDENTITY,                            SERVICE_DEV,    ril_request_device_identity},
    {RIL_REQUEST_EXIT_EMERGENCY_CALLBACK_MODE,               SERVICE_NULL,   NULL},
    {RIL_REQUEST_GET_SMSC_ADDRESS,                           SERVICE_MSG,    ril_request_get_smsc_address},
    {RIL_REQUEST_SET_SMSC_ADDRESS,                           SERVICE_MSG,    ril_request_set_smsc_address},
    {RIL_REQUEST_REPORT_SMS_MEMORY_STATUS,                   SERVICE_MSG,    ril_request_report_sms_memory_status},
    {RIL_REQUEST_REPORT_STK_SERVICE_IS_RUNNING,              SERVICE_SIM,    ril_request_report_stk_service_is_running},
    {RIL_REQUEST_CDMA_GET_SUBSCRIPTION_SOURCE,               SERVICE_NULL,   NULL},
    {RIL_REQUEST_ISIM_AUTHENTICATION,                        SERVICE_NULL,   NULL},
    {RIL_REQUEST_ACKNOWLEDGE_INCOMING_GSM_SMS_WITH_PDU,      SERVICE_MSG,    ril_request_acknowledge_incoming_gsm_sms_with_pdu},
    {RIL_REQUEST_STK_SEND_ENVELOPE_WITH_STATUS,              SERVICE_SIM,    ril_request_stk_send_envelope_with_status},
	{RIL_REQUEST_VOICE_RADIO_TECH,              SERVICE_NULL,    NULL},
	
//added 5 request imfo by Boris 20161128,20170401
#ifdef ANDROID5_1_SUPPORTED   
    {RIL_REQUEST_GET_CELL_INFO_LIST, SERVICE_NULL, NULL},
    {RIL_REQUEST_SET_UNSOL_CELL_INFO_LIST_RATE, SERVICE_NULL, NULL},
    {RIL_REQUEST_SET_INITIAL_ATTACH_APN, SERVICE_NULL, NULL},
    {RIL_REQUEST_IMS_REGISTRATION_STATE, SERVICE_NULL, NULL},
    {RIL_REQUEST_IMS_SEND_SMS, SERVICE_NULL, NULL},
#endif 
	 //added 114&115 request imfo by Boris 20170317 and 20170318,20170401
#ifdef ANDROID5_1_AND_6_0_SUPPORTED  

    {RIL_REQUEST_SIM_TRANSMIT_APDU_BASIC,                        SERVICE_PS,   ril_request_allow_data},
	{RIL_REQUEST_SIM_OPEN_CHANNEL,                               SERVICE_DEV,    ril_request_sim_open_channel_get_ziccid},

   	{RIL_REQUEST_NV_READ_ITEM,                   SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
   	{RIL_REQUEST_NV_WRITE_ITEM,                  SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
   	{RIL_REQUEST_NV_WRITE_CDMA_PRL,              SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
   	{RIL_REQUEST_NV_RESET_CONFIG,                SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
   	{RIL_REQUEST_SET_UICC_SUBSCRIPTION,          SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
   	{RIL_REQUEST_ALLOW_DATA,                     SERVICE_PS,   ril_request_allow_data},

#if 0 //20170509
	/**********chenjun 20170504**********/
	{RIL_REQUEST_START_LCE,                      SERVICE_PS,   ril_request_start_lce},//for all-winner 6.0 temp
    {RIL_REQUEST_STOP_LCE,                       SERVICE_PS,   ril_request_stop_lce},//for all-winner 6.0 temp
    {RIL_REQUEST_PULL_LCEDATA,                   SERVICE_PS,   ril_request_pull_lcedata},//for all-winner 6.0 temp
	/************************************/
#endif

#if 1 //20170509
   {RIL_REQUEST_GET_HARDWARE_CONFIG,                        SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
   {RIL_REQUEST_SIM_AUTHENTICATION,                       SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
   {RIL_REQUEST_GET_DC_RT_INFO,                        SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
   {RIL_REQUEST_SET_DC_RT_INFO_RATE,                        SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
   {RIL_REQUEST_SET_DATA_PROFILE,                        SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
   {RIL_REQUEST_SHUTDOWN,                        SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
   {RIL_REQUEST_GET_RADIO_CAPABILITY,                       SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
   {RIL_REQUEST_SET_RADIO_CAPABILITY,                        SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
   {RIL_REQUEST_START_LCE,                        SERVICE_PS,   ril_request_start_lce},//for all-winner 6.0 temp
   {RIL_REQUEST_STOP_LCE,                       SERVICE_PS,   ril_request_stop_lce},//for all-winner 6.0 temp
   {RIL_REQUEST_PULL_LCEDATA,                        SERVICE_PS,   ril_request_pull_lcedata},//for all-winner 6.0 temp
   {RIL_REQUEST_GET_ACTIVITY_INFO,                       SERVICE_PS,   ril_request_allow_data},//for all-winner 6.0 temp
  #endif                  
#endif
};

#ifdef ZTE_RIL_EXTEND
static const struct request_info    s_requests_ext[] =
{
//mod by zouxiaojie 2013119 for TSP 7510 modem and dual standby begin
    {RIL_REQUEST_EXT_BASE, SERVICE_NULL, NULL},
	{RIL_REQUEST_SET_PS_MODE,						 SERVICE_MM,	 ril_request_set_ps_mode},
	
	{RIL_REQUEST_SET_PS_TRANSFER,					 SERVICE_PS,	 ril_request_set_ps_transfer},
    {RIL_REQUEST_GET_PINPUK_RETRIES,             SERVICE_SIM,    ril_request_get_pinpuk_retries_zte},
    {RIL_REQUEST_DO_SIM_AUTH,                    SERVICE_SIM,    ril_request_do_sim_auth_zte},
    {RIL_REQUEST_DO_USIM_AUTH,                   SERVICE_SIM,    ril_request_do_usim_auth_zte},
    {RIL_REQUEST_SIM_SMS_CAPABILITY,             SERVICE_MSG,    ril_request_sim_sms_capability_zte},
//mod by zouxiaojie 2013119 for TSP 7510 modem and dual standby end
//add by ax for TSP 7510 begin
    {RIL_REQUEST_PLAY_CALLWAIT_TONE_ZTE,         SERVICE_CC,     ril_request_play_callwait_tone_zte},
    {RIL_REQUEST_SET_PREFERRED_PLMN_LIST_ZTE,    SERVICE_MM,     ril_request_set_preferred_plmn_list_zte},
    {RIL_REQUEST_GET_PREFERRED_PLMN_LIST_ZTE,    SERVICE_MM,     ril_request_get_preferred_plmn_list_zte},
	{RIL_REQUEST_GET_RECORD_NUM,                   SERVICE_SIM,    ril_request_get_record_num_zte},
	{RIL_REQUEST_GET_ICC_PHONEBOOK_RECORD_INFO,          SERVICE_SIM,    ril_request_get_icc_phonebook_record_info},
	{RIL_REQUEST_READ_ICC_CARD_RECORD,                   SERVICE_SIM,    ril_request_read_icc_card_record_zte},
	{RIL_REQUEST_WRITE_ICC_CARD_RECORD,           SERVICE_SIM,    requestWriteIccCardRecord},	
	{RIL_REQUEST_SET_CGATT,				SERVICE_PS,ril_request_set_cgactt_zte},
	{RIL_REQUEST_SET_CGSMS,				SERVICE_PS,ril_request_set_cgsms_zte},
	{RIL_REQUEST_DEACTIVATE_PDP_ZTE,	SERVICE_PS,ril_request_deactivate_zte},
	{RIL_REQUEST_SET_3GQOS_ZTE,			SERVICE_MM,ril_request_set_3gqos_zte},
	{RIL_REQUEST_QUERY_3GQOS_ZTE,		SERVICE_MM,ril_request_query_3gqos_zte},
    {RIL_REQUEST_GET_SN_ZTE,                     SERVICE_DEV,    ril_request_get_sn_zte},
//mod by zouxiaojie 2013119 for TSP 7510 modem and dual standby end
    {RIL_REQUEST_NEW_SMS_INDICATION,        SERVICE_NULL,    NULL},
    {RIL_REQUEST_GET_SIM_TYPE,        SERVICE_NULL,   NULL},
    {RIL_REQUEST_GET_MSISDN,          SERVICE_NULL,   NULL},
    {RIL_REQUEST_ATTACH_GPRS_ZTE,     SERVICE_PS,    ril_request_set_cgactt_zte},
	{RIL_REQUEST_DETACH_GPRS_ZTE,     SERVICE_PS,    ril_request_set_cgactt_zte},
	{RIL_REQUEST_SET_GSM_INFO_PERIODIC_MODE_ZTE,	  SERVICE_MM,	 ril_request_set_zemci_zte},
	{RIL_REQUEST_TURN_OFF_GSM_INFO_INDICATOR_ZTE,	  SERVICE_MM,	 ril_request_set_zemci_zte},
	{RIL_REQUEST_PLAY_DTMF_TONE,   SERVICE_SS,  ril_request_play_dtmf_tone},
	{RIL_REQUEST_SHUT_DOWN_RADIO,   SERVICE_MM,  ril_request_shut_down_radio},

	{RIL_REQUEST_SYN_PDP_CONTEXT, SERVICE_PS, ril_request_syn_pdp_context},
	{RIL_REQUEST_DEFINE_PDP_CONTEXT, SERVICE_PS, ril_request_define_pdp_context},
	{RIL_REQUEST_GET_ZPS_STAT,SERVICE_PS, ril_request_get_zps_stat},
	{RIL_REQUEST_GET_BGLTE_PLMN,SERVICE_NULL, NULL},
	{RIL_REQUEST_SET_ZFLAG, SERVICE_PS, ril_request_set_zflag},
};
#endif /* ZTE_RIL_EXTEND */

static const struct indication_info s_indications[] =
{
    //{"+CSQ:",               SERVICE_MM,    handle_csq},
    //{"+ZSQR:",              SERVICE_MM,    handle_csq},
    {"+CREG:",              SERVICE_MM,    handle_creg},
    {"+CGREG:",             SERVICE_MM,    handle_creg},
    {"+CEREG:",             SERVICE_MM,    handle_cereg},//modified by boris for telecom FDD LTE 20160810
    {"^MODE:",              SERVICE_MM,    handle_mode},
    //{"+NITZ:",              SERVICE_MM,    handle_nitz},
    {"+ZMMI:",              SERVICE_MM,    handle_zmmi},
    {"+CRING:",             SERVICE_CC,    handle_call_state_changed},
    {"RING",                SERVICE_CC,    handle_ring},
    {"+ZSRVING",                SERVICE_CC,    handle_zsrving},
    {"NO CARRIER",          SERVICE_CC,    handle_call_state_changed},
    {"CONNECT",             SERVICE_CC,    handle_connect},
    {"+CCWA:",              SERVICE_CC,    handle_call_state_changed},
    {"+CCCM:",              SERVICE_CC,    handle_cccm},
    {"+CLCC:",              SERVICE_CC,    handle_call_state_changed},
    {"+CLIP:",              SERVICE_CC,    handle_call_state_changed},
    {"+CNAP:",              SERVICE_CC,    handle_call_state_changed},
    {"^DSCI:",              SERVICE_CC,    handle_call_state_changed},
    {"DVTCLOSED",              SERVICE_CC,    handle_call_state_changed},

    {"+CGEV:",              SERVICE_PS,    handle_cgev},
    {"+ZGIPDNS:",           SERVICE_PS,    handle_zgipdns},
    {"+CMT:",               SERVICE_MSG,   handle_cmt},
    {"+CMTI:",              SERVICE_MSG,   handle_cmti},
    {"+CDS:",               SERVICE_MSG,   handle_cds},
    {"+ZMGSF:",              SERVICE_MSG,   handle_mmsg},//xichunyan 2012-07-12
    {"+CBM:",               SERVICE_MSG,   handle_cbm},
    {"+ZURDY:",             SERVICE_SIM,   handle_zurdy},
    {"+ZUFCH:",            SERVICE_SIM,   handle_zufch},
    {"+ZUSLOT:",            SERVICE_SIM,   handle_zuslot},
    {"+CSSI:",              SERVICE_SS,    handle_cssi},
    {"+CSSU:",              SERVICE_SS,    handle_cssu},
    {"+CUSD:",              SERVICE_SS,    handle_cusd},
    #ifdef ZTE_RIL_EXTEND
    //{"CONNECT_CSD",         SERVICE_CC, handle_connect_csd},
    {"^ORIG:",              SERVICE_CC,    handle_call_state_changed},
    {"^CONF:",              SERVICE_CC,    handle_call_state_changed},
    {"^CONN:",              SERVICE_CC,    handle_call_state_changed},
    {"^CEND:",              SERVICE_CC,    handle_call_state_changed},
    {"^DSCI:",              SERVICE_CC,    handle_call_state_changed},
    {"+ZCPI:",              SERVICE_CC,    handle_zcpi},

    {"+ZICCID:",            SERVICE_SIM,   handle_icc},                      //20161020   gelei   dingwei parturition
    {"+ZMSRI",              SERVICE_DEV,   handle_zmsri},
	{"+ZPBIC:",				SERVICE_SS,   handle_zpbic},
	{"+ZEMCI:",				SERVICE_MM,   handle_zemci},
    #endif /* ZTE_RIL_EXTEND */

};


/*************************************************************
 *  Lib called by ril-xxx.c
 **************************************************************/


void ril_handle_cmd_default_response_timeout(const char *cmd, RIL_Token token, long long msec)
{
    ATResponse  *response = NULL;

    int err = at_send_command_timeout(cmd, &response, msec);
    if (err < 0 || response->success == 0)
    {
        goto error;
    }

    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
    goto exit;
    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}

void ril_handle_cmd_sms_one_int_timeout(const char *cmd, const char *sms_pdu, const char *prefix, RIL_Token token, long long msec)
{
    ATResponse  *response = NULL;
    int result,
    err;
    char    *line;

    err = at_send_command_sms_timeout(cmd, sms_pdu, prefix, &response, msec);
    if (err < 0 || response->success == 0 || !response->p_intermediates)
    {
        goto error;
    }

    line = response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextint(&line, &result);
    if (err < 0)
    {
        goto error;
    }

    RIL_onRequestComplete(token, RIL_E_SUCCESS, &result, sizeof(result));

    goto exit;
    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}

RIL_RadioState getRadioState()
{
    return sState;
}

void setRadioState(RIL_RadioState newState)
{
    RIL_RadioState  oldState;

    pthread_mutex_lock(&s_state_mutex);

    oldState = sState;
    if (s_closed > 0)
    {
        /* If we're closed, the only reasonable state is RADIO_STATE_UNAVAILABLE
             * This is here because things on the main thread may attempt to change the radio state
             * after the closed event happened in another thread
             */
        newState = RADIO_STATE_UNAVAILABLE;
    }
    ALOGI("setRadioState: oldState=%d newState=%d", oldState, newState);
    if (sState != newState || s_closed > 0)
    {
    	/* 把新的状态给这个全局变量，但是这个变量是干什么用的? */
        sState = newState;
		/* 广播的方式唤醒多个线程 */
        pthread_cond_broadcast(&s_state_cond);
    }

    pthread_mutex_unlock(&s_state_mutex);


    /* Do these outside of the mutex */
    if (sState != oldState)
    {
    	/* 上报消息响应 */
        RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_RADIO_STATE_CHANGED, NULL, 0);
    }
}


/*************************************************************
 * Power on process related
 **************************************************************/

/**
 * Synchronous call from the RIL to us to return current radio state.
 * RADIO_STATE_UNAVAILABLE should be the initial state.
 */
static RIL_RadioState onCurrentState()
{
    return getRadioState();
}
/**
 * Call from RIL to us to find out whether a specific request code
 * is supported by this implementation.
 *
 * Return 1 for "supported" and 0 for "unsupported"
 */

static int onSupports(int requestCode)
{
    //@@@ todo
    return 1;
}

static void onCancel(RIL_Token token)
{
    //@@@todo
    ALOGI("onCancel is called, but not implemented yet\n");
}

static const char * onGetVersion(void)
{
    return "ZTE Ril 1.0";
}

static int writeAdjust(void)
{
    ATResponse  *response = NULL;
    char    *line, *result;
	int fd = -1,writeBytes = 0;

    int err = at_send_command_singleline("AT^ZADJUST?", "^ZADJUST:", &response);
    if (err < 0 || response->success == 0 || !response->p_intermediates)
    {
        return -1;
    }

    line = response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
    {
        return -1;
    }

    err = at_tok_nextstr(&line, &result);
    if (err < 0)
    {
        return -1;
    }
	ALOGD("writeAdjust: result = %s",result);
	fd = open ("/amt/product_flag", O_RDWR);
	if(fd < 0)
	{
        return -1;
    }
	writeBytes = write(fd, result, strlen(result));
    if (writeBytes < 0)
    {
        ALOGE("Error write device: %s,\n", strerror(errno));
        return -1;
    }
	return 0;
}
static void initializeCallback_sync_only(void *arg)
{
    //at_send_command("AT", NULL);
    return;
}

static void initializeCallback_mm(void *arg)
{
    ATResponse  *response = NULL;
    int err;

/* deleted by RenYimin_2015-11-12 because AT^SYSCONFIG=x,x,x,x cmd will be send by framework BEGIN */
//#ifdef RIL_VOLTE


	//err = at_send_command("AT^SYSCONFIG=17,0,1,2", NULL);// LTE prefer as default
	
//#else
	//if(standbymode()== 1)
	//{
	  // err = at_send_command("AT^SYSCONFIG=13,1,1,2", NULL);// TD prefer as default
	//}
	
	//err = at_send_command("AT+ZLTELC=0", NULL);// unlock cell
//#endif
/* deleted by RenYimin_2015-11-12 because AT^SYSCONFIG=x,x,x,x cmd will be send by framework END */

    /* system mode indicate set */
    err = at_send_command("AT^MODE=1", NULL);
    /* auto gprs attach */

	//if(standbymode() == 0){
		err = at_send_command("AT+ZGAAT=0", NULL);
	//}
    /* PLMN format, numeric */  
    err = at_send_command_timeout("AT+COPS=3,2", NULL, TIMEOUT_COPS);  //bufantong 20160829
    /* voice registration events */
    err = at_send_command("AT+CREG=2", &response);

    /* some handsets -- in tethered mode -- don't support CREG=2 */
    if (err < 0 || response->success == 0)
    {
        at_send_command("AT+CREG=1", NULL);
    }
    at_response_free(response);
    response = NULL;  
    /*LTE*/
    err = at_send_command("AT+CEREG=2", &response);
    if (err < 0 || response->success == 0)
    {
        at_send_command("AT+CEREG=1", NULL);
    }
    at_response_free(response);
    response = NULL;  

    /*  GPRS registration events */
    err = at_send_command("AT+CGREG=2", &response);
    if (err < 0 || response->success == 0)
    {
        at_send_command("AT+CGREG=1", NULL);
    }
    at_response_free(response);

    /* signal strength indicate */
    //at_send_command("AT+ZSQR=1,1", NULL);


    /* auto net state indication */
    err = at_send_command("AT+ZMMI=1", NULL);
	
    //at_send_command("AT+ZPSSTAT=1", NULL);//add by wangna for psmoving
	//correctStandmode();

    /*** Note:  CP can not store the power off state, so CP power on with airplane***/
	
	/* deleted by RenYimin_2015-11-12 because AT+CFUN=1 cmd will be send by framework BEGIN */
	//at_send_command("AT+CFUN=1", NULL);
	/* deleted by RenYimin_2015-11-12 because AT+CFUN=1 cmd will be send by framework END */

    return;
}

static void initializeCallback_cc(void *arg)
{

#ifdef RIL_VOLTE	
	at_send_command("AT+CVMOD=1", NULL);//VOIP_PREFERRED
#else
	at_send_command("AT+CVMOD=0", NULL);//CS_ONLY 
#endif

    /*  No auto-answer */
    at_send_command("ATS0=0", NULL);
	
    /*  Disable +CCCM ind msg */
    at_send_command("AT+CAOC=1", NULL);
	
    /* CRING:<type> instead of RING */
    //at_send_command("AT+CRC=1", NULL);
	struct timeval	currenttime;
	gettimeofday(&currenttime, NULL);
	char *cmdString;
	asprintf(&cmdString, "AT+SYCTIME=%u,%u", currenttime.tv_sec,currenttime.tv_usec);
	ALOGE("send AT command: %s",cmdString );
	at_send_command(cmdString, NULL);
    return;
}

static void initializeCallback_msg(void *arg)
{
#ifdef RIL_VOLTE	
		at_send_command("AT+ZSMSOIN=1", NULL);// support SMS over IP network 
#endif

    /*  SMS PDU mode */
    at_send_command("AT+CMGF=0", NULL);
    /*  MT msg saved in ME, enable the setting if SIM card already initialized */
    at_send_command("AT+CSMS=1", NULL);
    /*the AT will send after reading the user setting */
    at_send_command("AT+CNMI=2,2,0,1,0", NULL);

    return;
}

static void initializeCallback_ss(void *arg)
{
    /*  Call Waiting notifications */
    at_send_command("AT+CCWA=1", NULL);

    /*  +CSSU unsolicited supp service notifications */
    at_send_command("AT+CSSN=1,1", NULL);

    /*  no connected line identification */
    at_send_command("AT+COLP=0", NULL);

    /*  USSD unsolicited */
    at_send_command("AT+CUSD=1", NULL);

    at_send_command("AT+CLIP=1", NULL);

    return;
}

static void initializeCallback_ps(void *arg)
{
	#ifdef RIL_VOLTE	
	// UE EPS MODE: PS/CS
	at_send_command("AT+CEMODE = 2", NULL);

	// define default volte APN
	at_send_command("AT+CGDCONT=1,\"IP\",\"ZTE.COM\"", NULL);
	#endif

}

static void initializeCallback_sim(void *arg)
{ 
    int ret = -1;
	int err = -1;
	ATResponse  *response = NULL;
    /*  SIM/USIM card unsolicited */
	err = at_send_command("AT+ZRAP?", &response);
    if (response != NULL)
    {
        switch (at_get_cme_error(response))
		{
			case CME_SIM_NOT_INSERTED:
			{
			  property_set("ril.hasicccard", "0");
			  ALOGD("hasicccard set 0");
			  break;
			}
			default:
			{
			  property_set("ril.hasicccard", "1");
			  ALOGD("hasicccard set 1");
			}
		}
    }
	at_response_free(response);
    response = NULL; 
	
    at_send_command("AT+ZUSLOT=1", NULL);
    /*  SIM/USIM card initialize result unsolicited */
    at_send_command("AT+ZURDY=1", NULL);
	at_send_command("AT+ZSET=\"UE_TYPE\",1", NULL);
	//getAppType();
    /* Android added by GanHuiliang_2012-7-27 *** BEGIN */
    /* start initialize card */
    //at_send_command("AT+ZUINIT=0,1,2,0", NULL);  //E version
    /* Android added by GanHuiliang_2012-7-27 END */
	
    ret = writeAdjust();
	if(ret < 0) ALOGE("writeAdjust failed");
    return;
}


/**
 * Initialize everything that can be configured while we're still in
 * AT+CFUN=0
 */
void enque_initializeCallback()
{
    ALOGI("enter enque_initializeCallback!!");
    enque(getWorkQueue(SERVICE_MM), initializeCallback_mm, NULL);
    enque(getWorkQueue(SERVICE_CC), initializeCallback_cc, NULL);
    enque(getWorkQueue(SERVICE_MSG), initializeCallback_msg, NULL);
    enque(getWorkQueue(SERVICE_SS), initializeCallback_ss, NULL);
    enque(getWorkQueue(SERVICE_DEV), initializeCallback_sync_only, NULL);
    enque(getWorkQueue(SERVICE_PS), initializeCallback_ps, NULL);
    enque(getWorkQueue(SERVICE_SIM), initializeCallback_sim, NULL);
    enque(getWorkQueue(SERVICE_VT), initializeCallback_sync_only, NULL);
    ALOGI("leave enque_initializeCallback!!");
}

static void waitForClose()
{
    pthread_mutex_lock(&s_state_mutex);

    while (s_closed == 0)
    {
        pthread_cond_wait(&s_state_cond, &s_state_mutex);
    }

    pthread_mutex_unlock(&s_state_mutex);
}


static int GetCpPowerState()
{
    int pwr_fd = -1;
    char    pwr_state[8] = "";
    int pwr_len = 0;
    int ret = 0;

    pwr_fd = open("/sys/ap2cp/cp_ready", O_RDONLY);
    if (pwr_fd < 0)
    {
        ALOGE("GetCpPowerState error %d \n", pwr_fd);
        return -1;
    }

    pwr_len = read(pwr_fd, pwr_state, 8);

    close(pwr_fd);

    if (strstr(pwr_state, "1") != NULL)
    {
        ret = 1;
    }

    return ret;
}


static int SetCpPowerState(CP_Power_State PowerState)
{
    int pwr_fd = -1;
    char    pwr_buf[32] =
    {
        0,

    };
    int pwr_len = 0;

    pwr_fd = open("/sys/ap2cp/ap2cp_power", O_WRONLY);

    if (pwr_fd < 0)
    {
        ALOGE("Power on Modem error %d \n", pwr_fd);
        return -1;
    }

    switch (PowerState)
    {
        case POWER_ON:
        {
            pwr_len = snprintf(pwr_buf, sizeof(pwr_buf), "%s", "poweron");
            break;
        }

        case POWER_RESET:
        {
            pwr_len = snprintf(pwr_buf, sizeof(pwr_buf), "%s", "reset");
            break;
        }

        case POWER_OFF:
        {
            //pwr_len = snprintf(pwr_buf, sizeof(pwr_buf), "%s", "reset");
            break;
        }

        default:
        break;
    }

    write(pwr_fd, pwr_buf, pwr_len);

    close(pwr_fd);


    return 0;
}


static int reset_modem(void)
{
    int fd;
    int ret = -1;
    char    value = 0;

    fd = open("/sys/devices/platform/modem_control/hoston", O_RDWR);
    if (fd < 0)
    {
        return -1;
    }

    ret = read(fd, &value, 1);
    if (ret != 1)
    {
        close(fd);
        return -2;
    }

    if (value == '0')
    {
        ret = write(fd, "1", 2);
        ALOGD("%s: AP_RILD_USB_HOSTON:%d", __FUNCTION__, ret);
        if (ret < 0)
        {
            close(fd);
            return -3;
        }
    }

    close(fd);

    fd = open("/sys/devices/platform/modem_control/reset", O_RDWR);
    if (fd < 0)
    {
        return -4;
    }

    ret = write(fd, "1", 2);
    ALOGD("%s: AP_RILD_RESET_MODEM:%d", __FUNCTION__, ret);
    if (ret < 0)
    {
        close(fd);
        return -5;
    }

    close(fd);
    return 0;
}


/* 命令读取线程 */
/* Called on command or reader thread */
static void onATReaderClosed()
{
	/* 禁止所有的mobile接口 */
    disableAllMobileInterfaces();

    // readerLoop has close all channels, just need to wake up mainLoop
    ALOGD("onATReaderClosed ");
    s_closed = 1;
    iccReady = 0;
    ALOGD("ril.modemReseting is set 1 ");
    property_set("ril.modemReseting", "1");

	/* 设置状态 */
    setRadioState(RADIO_STATE_OFF);
	/* sim卡状态更改 */
	RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED, NULL, 0);
    property_set("ril.reset_manual", "1");//zxj add
    ALOGD("%s: reset_modem", __FUNCTION__);
}

/* Called on command thread */
static void onATTimeout(int channelID)
{
    // just close the timeout channel, readerLoop would detect bad channel then close all channels and exit.
    at_channel_close(channelID);
}

static int validate_baudrate(int baud_rate, int *value)
{
    unsigned int i;

    for (i = 0; i < (sizeof(baud_rates) / sizeof(tBaudRates)); i++)
    {
        if (baud_rates[i].baud_rate == baud_rate)
        {
            *value = baud_rates[i].termios_value;
            return(1);
        }
    }

    return(0);
}

int set_speed(int fd, struct termios *ti, int speed)
{
    if (cfsetospeed(ti, speed) < 0)
    {
        return -errno;
    }

    if (cfsetispeed(ti, speed) < 0)
    {
        return -errno;
    }

    if (tcsetattr(fd, TCSANOW, ti) < 0)
    {
        return -errno;
    }

    return 0;
}


static int wait_for_cp_ready(int fd)
{
    int readBytes = 0;
    char    readBuf[512] = "";
    int writeBytes = 0; 
    char    mux_cmd_str[] = "at\r\n";
    fd_set  rfds;
    struct timeval  tv;
    int ret = -1;

    while (1)
    {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec = 3;
        tv.tv_usec = 0; 
        writeBytes = write(fd, mux_cmd_str, strlen(mux_cmd_str));
        if (writeBytes < 0)
        {
            ALOGD("write device: %d,%s error!\n", writeBytes, mux_cmd_str);
            return -1;
        }

        ret = select(fd + 1, &rfds, NULL, NULL, &tv);       
        if (ret == -1)
        {
            break;
        }
        else if (ret)
        {
            memset(readBuf, 0x00, sizeof(readBuf)); 
            readBytes = read(fd, readBuf, 512);

            if (strstr(readBuf, "ZMSRI") != NULL || strstr(readBuf, "zmsri") != NULL)
            {
                ALOGD("recive +ZMSRI, CP is Ready");
            }

            if (strstr(readBuf, "ok") != NULL || strstr(readBuf, "OK") != NULL)
            {
                ALOGD("recive OK, wait_for_cp_ready successful");
                break;
            }
        }
        else
        {
            ALOGD("wait_for_cp_ready timeout\A3\A1 \n");
        }
    }

    return 0;
}

int reSetGsmCfgInfo(int fd)
{
    int ret = -1;
	struct gsm_config   gsmcfg;
    ret = ioctl(fd, GSMIOC_GETCONF, &gsmcfg);
    if (ret < 0)
    {
        ALOGD("GSMIOC_GETCONF error!\n");
        return -1;
    }
    gsmcfg.encapsulation = n_gsmcfg.encapsulation;
    gsmcfg.initiator = n_gsmcfg.initiator;
    if (n_gsmcfg.mru != 0)
    {
        gsmcfg.mru = n_gsmcfg.mru;
    }
    if (n_gsmcfg.mtu != 0)
    {
        gsmcfg.mtu = n_gsmcfg.mtu;
    }
    if (n_gsmcfg.t1 != 0)
    {
        gsmcfg.t1 = n_gsmcfg.t1;
    }
    if (n_gsmcfg.t2 != 0)
    {
        gsmcfg.t2 = n_gsmcfg.t2;
    }
    if (n_gsmcfg.t3 != 0)
    {
        gsmcfg.t3 = n_gsmcfg.t3;
    }
    if (n_gsmcfg.n2 != 0)
    {
        gsmcfg.n2 = n_gsmcfg.n2;
    }
    if (n_gsmcfg.k != 0)
    {
        gsmcfg.k = n_gsmcfg.k;
    }
    if (n_gsmcfg.i != 0)
    {
        gsmcfg.i = n_gsmcfg.i;
    }

    ret = ioctl(fd, GSMIOC_SETCONF, &gsmcfg);
    if (ret < 0)
    {
        ALOGD("GSMIOC_SETCONF error!\n");
        return -1;
    }
    return 0;
}



void openGsmttyFdchannel()
{
	int loopi = 0;

    for (loopi = 1; loopi <= channel_count; loopi++)
    {
        int bValue;
        char    gsmtty_dev[30];

        sprintf(gsmtty_dev, "/dev/gsmtty%d", loopi);
        gsmtty_fd[loopi] = open(gsmtty_dev, O_RDWR);
        if (gsmtty_fd[loopi] < 0)
        {
            ALOGD("Can not open %s, error code=%d %s\n", gsmtty_dev, errno, strerror(errno));
        }
        else
        {
            ALOGD("Open %s, fd: %d success", gsmtty_dev, gsmtty_fd[loopi]);
        }
        tcgetattr(gsmtty_fd[loopi], &oldtio); // save current port settings
        newtio = oldtio;
        newtio.c_cflag = CS8 | CLOCAL | CREAD;
        newtio.c_iflag = IGNPAR;
        newtio.c_oflag = 0;
        newtio.c_lflag = 0;       //!ICANON;
        newtio.c_cc[VMIN] = 1;
        newtio.c_cc[VTIME] = 0;
        validate_baudrate(baudrate, &bValue);
        set_speed(gsmtty_fd[loopi], &newtio, bValue);
        at_channel_open(/*descriptions[i].channelID*/loopi - 1, gsmtty_fd[loopi]);
    }
}

static int at_open_mux_channel(int fd)
{
    int ret = -1, ldisc = N_GSM0710, writeBytes = 0, readBytes;
    char    mux_cmd_str[] = "AT+CMUX=0\r";
    struct timeval  tv;
    fd_set  readfds;
    char    recv_buf[512] = "";
    char    readBuf[2048];

    writeBytes = write(fd, mux_cmd_str, strlen(mux_cmd_str));
    if (writeBytes < 0)
    {
        ALOGE("Error write device: %s,\n", strerror(errno));
        return -1;
    }
	readBytes = read(fd, recv_buf, 512);
	if(readBytes < 0){
	    ALOGD("Read error!\n");
	    return -1;
	}
	else
	{
	    ALOGD("success to read count=%d, %s",readBytes, recv_buf);
	}
    if (strstr(readBuf, "ZMSRI") != NULL || strstr(readBuf, "zmsri") != NULL)
    {
        ALOGD("recive +ZMSRI, CP is Ready");
    }
    if (strstr(recv_buf, "OK") != NULL)
    {
        ALOGD("Changed into MUX mode\n");
    }
    else
    {
        ALOGI("Changed into Normal mode\n");
        return -1;
    }
    ret = ioctl(fd, TIOCSETD, &ldisc);
    if (ret < 0)
    {
        ALOGD("Failed to set N_GSM0710 %d\n", ret);
        ALOGD("error code=%d %s\n", errno, strerror(errno));
        return -1;
    }
	ret = reSetGsmCfgInfo(fd);
    if(ret == -1)
    {
		return -1;
    }
	openGsmttyFdchannel();
    return 0;
}

/*Open the UART channel*/
static int at_open_hardware_devices(char *dev_name)
{
    int serial_fd = -1;
    static struct termios   oldtio,
    newtio;
    int bValue;
    int ret = -1;
    char    AT_buf[] = "AT\r";

    char    recv_buf[512] = "";
    int readBytes;

    while (1)
    {
        serial_fd = open(dev_name, O_RDWR);

        if (serial_fd < 0)
        {
            ALOGD("Cannot open ttydev %s\n", dev_name);
            sleep(2);
        }
        else
        {
            ALOGD("ttydev %s open ok !, fd=%d\n", dev_name, serial_fd);
            break;
        }
    }

    tcgetattr(serial_fd, &oldtio); // save current port settings
    newtio = oldtio;
    newtio.c_cflag = CS8 | CRTSCTS | CLOCAL | CREAD;
    //newtio.c_cflag = CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    newtio.c_lflag = 0;       //!ICANON;

    newtio.c_cc[VMIN] = 1;
    newtio.c_cc[VTIME] = 0;
    validate_baudrate(baudrate, &bValue);

    set_speed(serial_fd, &newtio, bValue);

    return serial_fd;
}

void at_close_mux_channel(void)
{
    int loopi = 0;
    int ret = -1;

    //for(loopi = 1; loopi < GSMTTY_MAX; loopi++)
    for (loopi = 1; loopi <= channel_count; loopi++)
    {
        int bValue;
        char    gsmtty_dev[30];

        sprintf(gsmtty_dev, "/dev/gsmtty%d", loopi);


        ALOGD("Try to close %s ", gsmtty_dev);

        ret = close(gsmtty_fd[loopi]);
        if (ret != 0)
        {
            ALOGD("Can not close %s, error code=%d %s\n", gsmtty_dev, errno, strerror(errno));
        }
        else
        {
            ALOGD("success close %s, fd: %d", gsmtty_dev, gsmtty_fd[loopi]);
        }
    }

    return;
}


static void usage(char *s)
{
#ifdef RIL_SHLIB
    fprintf(stderr, "reference-ril requires: -p <tcp port> or -d /dev/tty_device\n");
#else
    fprintf(stderr, "usage: %s [-p <tcp port>] [-d /dev/tty_device]\n", s);
    exit(-1);
#endif
}


static int enable_mux(char *dev_name)
{
    int fd;
    int displ_num = 7;
    char    writeBuf[1024];
    int writeBytes;
    char    readBuf[2048];
    int readBytes;
    int ret;

    fd = open("/dev/ttySPI0", O_RDWR);
    if (fd < 0)
    {
        ALOGE("Error openning device: %s,%s:%d\n", strerror(errno), dev_name, fd);
        return 0;
    }
    sprintf(writeBuf, "%s", "AT+CMUX=0\r\n");
    writeBytes = write(fd, writeBuf, strlen(writeBuf));

    if (writeBytes < 0)
    {
        ALOGE("Error write device: %s,%s:%d\n", strerror(errno), dev_name, fd);
    }
    ALOGI("Write %d bytes:%s !!\n", writeBytes, writeBuf);
    readBytes = read(fd, readBuf, 512 * 1);
    if (readBytes < 0)
    {
        printf("Error write device: %s,%s:%d\n", strerror(errno), dev_name, fd);
        return -1;
    }
    if (strstr(readBuf, "OK") != NULL)
    {
        ALOGI("Changed into MUX mode\n");
        ret = ioctl(fd, TIOCSETD, &displ_num);
        modem_mode = MUX_MODE;

        ret = 0;
    }
    else
    {
        ALOGI("Changed into Normal mode\n");
        modem_mode = NORMAL_MODE;

        close(fd);
        ret = 0;
    }
    return ret;
}

int rollZteRildStartFlag()
{
	// Give initializeCallback a chance to dispatched, since we don't presently have a cancellation mechanism
	sleep(1);
	waitForClose();
	ALOGI("Re-opening after close");
	
	for (; ;)
    {
	    char	zterild_startflag[PROPERTY_VALUE_MAX];
		if (0 == property_get("ril.startflag", zterild_startflag, "0"))
		{
			ALOGD("RILD get property failed \n");
			return 0;
		} 
		if (0 == strcmp(zterild_startflag, "1"))
		{
			break;
		}
		else
		{
			sleep(1);
		}
	}
	return 1;
}





/* added by RenYimin_2015-11-12 for AP and CP communication BEGIN */
/****************************************************
* judge  AT Response
* return value:
*  -1: operation fail
*   0: right Response waiting for
*   1: not right Response
****************************************************/
static int judge_at_response(fd_set readfds, char *ttydev)
{
    int ret = -1;
    char readbuf[MAX_AT_RESPONSE_LENGTH+2] = {0};

    if((ttydev_fd < 0)||(ttydev == NULL))
    {
        return -1;
    }
    if(FD_ISSET(ttydev_fd, &readfds)) 
    {
        ret = read(ttydev_fd, readbuf, sizeof(readbuf));
        if (ret < 0) 
        {
            ALOGD("read error on device %s %d", ttydev, ret);
            return 0;
        } 
        else if((ret > 0) &&(ret < MAX_AT_RESPONSE_LENGTH))
        {
            readbuf[ret] = '\0';
            ALOGD("< %s", readbuf);
            if(strstr(readbuf, "OK")) 
            {
                return 0;
            }
        }
    }
    return 1;
}

/****************************************************
* send cmd AT
* return value:none
****************************************************/
static int send_cmd_at(char *ttydev)
//int send_cmd_at(char *ttydev)
{
    struct timeval tv;        
    int ret = -1;
    int writeBytes = -1;
    int totalBytes = -1;
    fd_set readfds;
    if((ttydev_fd < 0)||(ttydev == NULL))
    {
        ALOGD("send_cmd_at error.\n");
        return -1;
    }
	memset(&tv, 0, sizeof(tv));
	tv.tv_sec = 1;
	ALOGD("> AT");
	totalBytes = strlen("AT\r\n") +2;
	writeBytes = write(ttydev_fd, "AT\r\n", totalBytes);
	if(writeBytes != totalBytes)
    {
        ALOGD("Error write AT.\n");
        return -1;
    }
    FD_ZERO(&readfds);
    FD_SET(ttydev_fd,&readfds);
    while (1) 
    {
        ret = select(ttydev_fd + 1, &readfds, NULL, NULL, &tv);
        if(ret == 0) 
        {
            ALOGD("Select timeout");
            return -1;
        } 
        else if(ret < 0) 
        {
            ALOGD("Select error");
            return -1;
        } 
        else 
        {
            ret = judge_at_response(readfds, ttydev);
            if(ret == 0)
            {
                return 1;
            }
        }
    }
}


/****************************************************
* set ttydev's Tcsanow Attr
* return value: same to tcsetattr
*  0: operation success
*  <0: operation fail 
****************************************************/
static int set_tcsanow_attr(int fd, int baudrate, struct termios oldtio)
{
    int ret = -1;
    int i = 0;
	int baudratesarry[14][2] =
    {
        {57600,  B57600},  
        {115200, B115200}, 
        {230400, B230400}, 
        {460800, B460800}, 
        {500000, B500000}, 
        {576000, B576000}, 
        {921600, B921600}, 
        {1000000,B1000000},
        {1152000,B1152000},
        {2000000,B2000000},
        {2500000,B2500000},
        {3000000,B3000000},
        {3500000,B3500000},
        {4000000,B4000000}
    };
    int rowcount = sizeof(baudratesarry)/(2*sizeof(int));
    struct termios newtio;
    
    newtio = oldtio;
	
    //newtio.c_cflag = CS8 | CREAD | CRTSCTS | CLOCAL;
    newtio.c_cflag = CS8 | CREAD | CLOCAL;

    for (i = 0; i<rowcount; i++)
    {
        if(baudrate == baudratesarry[i][0])
        {
            newtio.c_cflag |= baudratesarry[i][1];
            break;
        }
    }
    if(i == rowcount)
    {
        newtio.c_cflag |= B115200;
    }
    newtio.c_lflag = ICANON;
    //newtio.c_cc[VEOL] = 0x0d;
	newtio.c_iflag = IGNPAR;
    newtio.c_oflag = 0;
    //newtio.c_lflag = 0;
    newtio.c_cc[VMIN] = 1;
    newtio.c_cc[VTIME] =0;
	
    tcflush(fd, TCIFLUSH);
    ret = tcsetattr(fd, TCSANOW, &newtio);
    return ret;
}



static void open_ttydev(void)
{
	int ret = -1;
	int baudrate = 115200;;
	
	// loop open ttyUSB0,util OK 
	while(ttydev_fd < 0)
	{
		struct termios  ios;
		struct termios oldtio;
		
		ttydev_fd = open(TTY_DEV_NAME, O_RDWR); 
		if (ttydev_fd < 0) 
		{
			ALOGE("%s,%s,%d,error:%s",__FILE__,__FUNCTION__,__LINE__,strerror(errno));
			sleep(1);
			continue;
		}

		ALOGD("Open %s, ttydev_fd: %d success", TTY_DEV_NAME, ttydev_fd);

		//setting boudrate
		ret = tcgetattr(ttydev_fd, &oldtio);
		if(ret < 0) 
		{
			ALOGE("%s,%s,%d,error:%s",__FILE__,__FUNCTION__,__LINE__,strerror(errno));
			return -1;
		}
		
		ret = set_tcsanow_attr(ttydev_fd, baudrate, oldtio);
		if (ret < 0) 
		{
			ALOGE("%s,%s,%d,error:%s",__FILE__,__FUNCTION__,__LINE__,strerror(errno));
			return -1;
		}
		
		tcgetattr(ttydev_fd, &ios);
		ios.c_iflag = IGNPAR;
		ios.c_lflag = 0;  /* disable ECHO, ICANON, etc... */
		tcsetattr(ttydev_fd, TCSANOW, &ios);

	}

}


static int send_at_command(void)
{
	#define  MAX_TRY_TIMES 10    //bufantong_20161008
	int ret = -1;
	int retry;
	
	// send AT ,util ACK OK
	for(retry = 0; retry<MAX_TRY_TIMES;++retry)
	{
		ret = send_cmd_at(TTY_DEV_NAME);
		if (0 > ret)
	    	{
	       	ALOGD("send_cmd_at error");
	        	sleep(1);	
	        	continue;
	    	}
		else 
		{
			break;
		}
	}
	
	if(MAX_TRY_TIMES  == retry)
	{
		return -1;
	}
	return 0;
}
/* added by RenYimin_2015-11-12 for AP and CP communication END */



static void * mainLoop(void *param)
{
/* added by RenYimin_2015-11-12 for AP and CP communication BEGIN */
    /**
     * @pthread_setname_np: pthread_setname_np()和pthread_getname_np()，
     *  可以在进程中设置和读取其他线程的名字.
     * @pthread_self: 获得线程自身的ID
     */
    /* 设置当前线程的名字 */
    pthread_setname_np(pthread_self(), "RIL-ZTE");
	/* 线程上下文 */
    ThreadContext   context = {"RIL-ZTE", -1, 0};
	
    setThreadContext(&context);
    ALOGD("entering RIL-ZTE ");
	/* 设置关? */
    at_set_on_reader_closed(onATReaderClosed);
	
    at_set_on_timeout(onATTimeout);

    //sleep(1);//adde by bufantong for DL port emun,20160810
	//ALOGD(" =================sleep(1)===================== ");
	
    for (; ;)
    {
        //open ttyUSB0,set baudrate
        open_ttydev();
        if(-1 == send_at_command())
        {
            return NULL;
        }
        at_channel_open(CHANNEL_00, ttydev_fd);
        
        ALOGD("pthread_cond_signal unfinish ");
        pthread_cond_signal(&s_start_cond);
        ALOGD("pthread_cond_signal finish ");
/* added by RenYimin_2015-11-12 for AP and CP communication END */        
        s_closed = 0;
        if (at_channel_init(onUnsolicited) < 0)
        {
            ALOGE("at_channel_init error %d \n");
            return 0;
        }
        //sleep(5);
        enque_initializeCallback();
        setRadioState(RADIO_STATE_OFF);
        //ret = rollZteRildStartFlag();
        waitForClose();
        return 0;
    
    }
}



void * handle_eeh(char *value, int len, void *data)
{
    /*  if(strcmp("ASSERT", value) == 0)
        {
            onReaderClosed();
            ALOGI("Colse SPI TTY!\n");
            close(spittyfd);
        }
        exit(0);*/
    return NULL;
}

#ifdef RIL_SHLIB

pthread_t   s_tid_mainloop;



const RIL_RadioFunctions * RIL_Init(const struct RIL_Env *env, int argc, char **argv)
{
    int ret;
    int fd = -1;
    int opt;
    pthread_attr_t  attr;
    char    *modem = NULL;

    ALOGI("RIL_Init argv:%s", *argv);
	/* added by RenYimin_2015-11-12 for AP and CP communication BEGIN */
    pthread_cond_init(&s_start_cond, NULL);
    pthread_mutex_init(&s_start_mutex, NULL);
	/* added by RenYimin_2015-11-12 for AP and CP communication END */
	/* env是上层rild.c中传递进来的参数 */
    s_rilenv = env;

    /* [Jerry] the arg "-d /dev/ttyS0" defined in /<nfsroot>/system/build.prop is not used now.
     *            dev name is defined in descriptions[i].ttyName, and opened in mainLoop()
     */
    modemType = TSP_CP;
    //modem_mode = NORMAL_MODE;

    ALOGI("Current CP version:%d,%d", modemType, modem_mode);
    ALOGI("Current ril so version is"VER_TIME);

	/* 设置通道描述符 */
    set_channles(modemType, modem_mode);

	/* 通道结构体 */
    init_all_channel_struct();
	/**
	 * 创建线程特有的值
	 * 具体访问:http://www.jianshu.com/p/d52c1ebf808a
	 */
    pthread_key_create(&sKey, NULL);
	/**
	 * 创建队列，用上面的通道
	 */
    createWorkQueues();

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    ret = pthread_create(&s_tid_mainloop, &attr, mainLoop, NULL);
    pthread_attr_destroy(&attr);
	/* added by RenYimin_2015-11-12 for AP and CP communication BEGIN */
    ALOGD("pthread_cond_wait  s_start_cond unfinish ");
    pthread_cond_wait(&s_start_cond, &s_start_mutex);
    ALOGD("pthread_cond_signal  s_start_cond finish ");
	/* added by RenYimin_2015-11-12 for AP and CP communication END */
	
    
#ifdef TTY_USB_CHECK_AND_RESET  //bufantong 2017 0105
{
        int ret = 0;
		//pthread_t tid;
        pthread_attr_t attr_r;
 
        RLOGD( "RIL_Init check dev thread");
        pthread_attr_init (&attr_r);
        pthread_attr_setdetachstate(&attr_r, PTHREAD_CREATE_DETACHED);
        ret = pthread_create(&s_tid_ata, &attr_r, check_dev_loop, NULL);
		pthread_attr_destroy(&attr_r);
        if (ret < 0) {
            RLOGD( "RIL_Init check dev thread fail");
            perror ("pthread_create");
        }
}
#endif
    return &s_callbacks;
}
#else /* RIL_SHLIB */
int main(int argc, char **argv)
{
    int ret;
    int fd = -1;
    int opt;

    while (-1 != (opt = getopt(argc, argv, "p:d:")))
    {
        switch (opt)
        {
            case 'p':
            s_port = atoi(optarg);
            if (s_port == 0)
            {
                usage(argv[0]);
            }
            ALOGI("Opening loopback port %d\n", s_port);
            break;

            case 'd':
            s_device_path = optarg;
            ALOGI("Opening tty device %s\n", s_device_path);
            break;

            case 's':
            s_device_path = optarg;
            s_device_socket = 1;
            ALOGI("Opening socket %s\n", s_device_path);
            break;

            default:
            usage(argv[0]);
        }
    }

    if (s_port < 0 && s_device_path == NULL)
    {
        usage(argv[0]);
    }

    RIL_register(&s_callbacks);

    mainLoop(NULL);

    return 0;
}

#endif /* RIL_SHLIB */

const struct indication_info * getIndicationHandler(const char *s)
{
    size_t  i;

    for (i = 0; i < sizeof(s_indications) / sizeof(s_indications[0]); i++)
    {
        if (strStartsWith(s, s_indications[i].prefix))
        {
            return &s_indications[i];
        }
    }

    return NULL;
}

RIL_AppStatus	app_status_sim =
{
	RIL_APPTYPE_SIM,
	RIL_APPSTATE_DETECTED,
	RIL_PERSOSUBSTATE_UNKNOWN,
	NULL,
	NULL,
	0,
	RIL_PINSTATE_UNKNOWN,
	RIL_PINSTATE_UNKNOWN
};

RIL_AppStatus	app_status_usim =
{
	RIL_APPTYPE_USIM,
	RIL_APPSTATE_DETECTED,
	RIL_PERSOSUBSTATE_UNKNOWN,
	NULL,
	NULL,
	0,
	RIL_PINSTATE_UNKNOWN,
	RIL_PINSTATE_UNKNOWN
};

void needContinueGetSimStatus(RIL_Token token)
{
	// SIM NOT READY
	RIL_CardStatus	*p_card_status;
	RIL_AppType type = getKeepAppType();
	ALOGW("needContinueGetSimStatus RIL_AppType type(%d)\n", type);

	/* Allocate and initialize base card status. */
	p_card_status = malloc(sizeof(RIL_CardStatus));
	p_card_status->card_state = RIL_CARDSTATE_PRESENT;
	p_card_status->universal_pin_state = RIL_PINSTATE_UNKNOWN;
	p_card_status->gsm_umts_subscription_app_index = 0;
	p_card_status->cdma_subscription_app_index = -1;
	p_card_status->ims_subscription_app_index = -1;
	p_card_status->num_applications = 1;
	if(type == RIL_APPTYPE_USIM)
	{
	    p_card_status->applications[0] = app_status_usim;
	}
	else
	{
	    p_card_status->applications[0] = app_status_sim;
	}		
	RIL_onRequestComplete(token, RIL_E_SUCCESS, (char *) p_card_status, sizeof(RIL_CardStatus));

	free(p_card_status);
	return;
}

void needContinueGetimsi(RIL_Token token)
{
	char propertieContent[PROPERTY_VALUE_MAX] = {0};

    property_get("ril.imsi", propertieContent, "0");
	
	ALOGD("needContinueGetimsi, property_get ril.imsi, propertieContent=%s\n", propertieContent);
    if(0 != strcmp(propertieContent,"0"))
    {   
	  RIL_onRequestComplete(token, RIL_E_SUCCESS, (char *) propertieContent, sizeof(propertieContent));
    }
	else
	{
	  RIL_onRequestComplete(token, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
	}
	return;
}

// called from work queue process
// return value: 1 continue, 0 not continue
int needContinueProcess(int request, void *data, size_t datalen, RIL_Token token)
{


	ALOGD("needContinueProcess, request = %d sState = %d \n", request,sState);
    if ((request == RIL_REQUEST_SCREEN_STATE)||
		(request == RIL_REQUEST_RESET_RADIO)
		)
    {
    	
	if((sState == RADIO_STATE_UNAVAILABLE)||(sState == RADIO_STATE_OFF)){
    	ALOGW("needContinueProcess  request(%d) handler\n", request);}
        return 1;
    }



	
    /* Handle RIL request when radio state is UNAVAILABLE */
    if (sState == RADIO_STATE_UNAVAILABLE)
    {
    		ALOGD("needContinueProcess, sState == RADIO_STATE_UNAVAILABLE");
        if (request == RIL_REQUEST_GET_SIM_STATUS)
        {
        		ALOGD("needContinueProcess, sState == RADIO_STATE_UNAVAILABLE,request == RIL_REQUEST_GET_SIM_STATUS");
				
			needContinueGetSimStatus(token);
            return 0;
        }
        else if (request == RIL_REQUEST_GET_IMSI)
        {
        		ALOGD("needContinueProcess, sState == RADIO_STATE_UNAVAILABLE,request == RIL_REQUEST_GET_IMSI");
			needContinueGetimsi(token);
            return 0;
        }
        else
        {
        	ALOGD("needContinueProcess, sState == RADIO_STATE_UNAVAILABLE,request == RIL_REQUEST_GET_SIM_STATUS");
            RIL_onRequestComplete(token, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
            return 0;
        }
    }

    /* Handle RIL request when radio state is OFF */
    else if (sState == RADIO_STATE_OFF)
    {
    	ALOGD("needContinueProcess, sState == RADIO_STATE_OFF");
        if (request == RIL_REQUEST_GET_SIM_STATUS)
        {
		ALOGD("needContinueProcess, sState == RADIO_STATE_OFF,request == RIL_REQUEST_GET_SIM_STATUS");
		
			needContinueGetSimStatus(token);
            return 0;
        }
		else if (request == RIL_REQUEST_GET_IMSI)
        {
        ALOGD("needContinueProcess, sState == RADIO_STATE_OFF,request == RIL_REQUEST_GET_IMSI");
		
			needContinueGetimsi(token);
            return 0;
        }
        else if (request == RIL_REQUEST_RADIO_POWER || request == RIL_REQUEST_SMS_ACKNOWLEDGE)
        {
        	 ALOGD("needContinueProcess, sState == RADIO_STATE_OFF,request == RIL_REQUEST_RADIO_POWER || request == RIL_REQUEST_SMS_ACKNOWLEDGE");
            //onRequest_dev(request, data, datalen, token);
            return 1;
        }
        else
        {
        	
            RIL_onRequestComplete(token, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
            return 0;
        }
    }
    return 1;
}

static void ProcessRequest(void *arg)
{
    RILRequest  *r = (RILRequest    *) arg;
    RIL_Token   token = r->token;
    int request = r->request;
    const struct request_info   *ri;
    const struct service_info   *si;

#ifdef ZTE_RIL_EXTEND
    ri = request < RIL_REQUEST_EXT_BASE ? &s_requests[request] : &s_requests_ext[request - RIL_REQUEST_EXT_BASE];
#else
    ri = &s_requests[request];
#endif 

    //ALOGD("%s: %s, token:%p", __FUNCTION__, requestToString(r->request), r->token);
    if (!needContinueProcess(r->request, r->data, r->datalen, r->token))
    {
        ALOGI("%s: do not need to continue to process, token:%p, r->request=%d", __FUNCTION__, r->token, r->request);
        freeRILRequest(r);
        return;
    }

    si = &s_services[ri->service];

    if (si->pre_process_request)
    {
        si->pre_process_request(r->request, r->data, r->datalen, r->token);
    }

    if (ri->handler)
    {
        ri->handler(r->request, r->data, r->datalen, r->token);
    }
    else
    {
        ALOGW("invalid request(%d) handler\n", request);
        RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
    }

    if (si->post_process_request)
    {
        si->post_process_request(r->request, r->data, r->datalen, r->token);
    }

    freeRILRequest(r);
    return;
}

/**
 * Call from RIL to us to make a RIL_REQUEST
 *
 * Must be completed with a call to RIL_onRequestComplete()
 *
 * RIL_onRequestComplete() may be called from any thread, before or after
 * this function returns.
 *
 * Will always be called from the same thread, so returning here implies
 * that the radio is ready to process another command (whether or not
 * the previous command has completed).
 */

static void onRequest(int request, void *data, size_t datalen, RIL_Token token)
{
    const struct request_info   *ri;

#ifdef ZTE_RIL_EXTEND
    ri = request < RIL_REQUEST_EXT_BASE ? &s_requests[request] : &s_requests_ext[request - RIL_REQUEST_EXT_BASE];
#else
    ri = &s_requests[request];
#endif


    ALOGD(" %s: request = %d %s, token:%p", __FUNCTION__, request,requestToString(request), token);

    if (ri->service == SERVICE_NULL || ri->handler == NULL)
    {
        ALOGW("invalid request: %d\n", request);
        RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
        return;
    }
    if((ri->service == SERVICE_CC)&&(!((request == RIL_REQUEST_DTMF_START)||(request == RIL_REQUEST_DTMF_STOP))))
    {
       dtmf_end = 1;
	}
    enque(getWorkQueue(ri->service), ProcessRequest, (void *) newRILRequest(request, data, datalen, token));
    return;
}

/**
 * Called by atchannel when an unsolicited line appears
 * This is called on atchannel's reader thread. AT commands may
 * not be issued here
 */
static void onUnsolicited(const char *s, const char *smsPdu)
{
    const struct indication_info    *pi = NULL;

    //ALOGI("onUnsolicited:%s", s);

    /* Ignore unsolicited responses until we're initialized.
     * This is OK because the RIL library will poll for initial state
     */

    pi = getIndicationHandler(s);
    if (pi && pi->handler)
    {
        pi->handler(s, smsPdu);
    }
    else
    {
        ALOGW("%s: Unexpected indication\n", __FUNCTION__);
    }
}

