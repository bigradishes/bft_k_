/******************************************************************************
 * *(C) Copyright 2012 ZTE International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/

#include <cutils/properties.h>
#include <sys/atomics.h>
#include <sys/time.h>
#include "at_tok.h"
#include "atchannel.h"
#include "zte-ril.h"
#include "misc.h"
#include "ril-requestdatahandler.h"
#include "work-queue.h"

extern void updateRadioState(void);

/************bufantong 20161223 17.29*******/   
extern char mcc_mnc[][20];
extern int cm_cu_ct;
/**********************/


//#define DIANXIN  //added 20160909
//#define LIANTONG //added 20160928
#define ZX_DT_RAT_HANDOVER
#define DIANXIN_WEAKSIGNAL_PLUS_NUM		15  //added by Boris 20160922
#define DIANXIN_MIDSIGNAL_PLUS_NUM		10  //modified by Boris 20160922

#define ZTERILD_GSM_CREG_STATE  "ril.gsmcreg"

typedef struct RegState_s
{
    int stat;
    int lac;
    int cid;
} RegState;

typedef struct OperInfo_s
{
    int mode;
    char    operLongStr[20];
    char    operShortStr[10];
    char    operNumStr[10];
    int act;
} OperInfo;

static RegState sCregState = { -1, -1, -1 }, sCgregState = { -1, -1, -1 };
static OperInfo sOperInfo = { -1, "\0", "\0", "\0", -1 };
static int sScreenState = 0;   //default screen state = OFF, it will turn to ON after RIL is initialized
static int sCSQ[2] = { 99, 99 };
static int sys_mode = -1;/*LTE*/ //modified from 0 by LiaoBin 20161125 for net search

static int check_creg_crerg_ = 0;

struct timeval lastNitzTimeReceived;
char lastNitzTime[18];

static int  sCellNumber = 0;
#define MAX_NEIGHBORING_CELLS 6 //max neighboring cell number is set as 6 in defualt
static RIL_NeighboringCell  sNeighboringCell[MAX_NEIGHBORING_CELLS]; 

static int airPlaneMode = 0;
extern int  reset_zte_ril;
#define ZTERILD_PDP_CGDCONT "ril.cgdcont" //add for pdp cgdcont 051368

//added by LiaoBin & Boris 20161125 for net search
static pthread_t s_tid_sysinfo;
#ifdef CHECK_SYSINFO_ONCE  
void check_sysinfo();
#endif    //gelei  20161209

#ifdef CHECK_SYSINFO
void *check_sysinfo(void *arg);
#endif      //gelei 20161209



int getCurNwMode()
{
	ALOGD("getCurNwMode =%d",sys_mode);
	return sys_mode;
}
/* Parse  AT reply of +CREG or +CGREG
 * Output: responseInt[0] : <stat>
          responseInt[1] : <lac>
          responseInt[2] : <cid>
          responseInt[3] : <AcT>
 */
 //added  by liutao 20131203 for TSP 7510 modem and dual standby start
void ril_request_set_3gqos_zte(int request, void *data, size_t datalen, RIL_Token token)
{
	char *cmd = NULL;
    int err = 0;
    char* traffic_class = ((char**)data)[0];
    char* ul_bitrate = ((char**)data)[1];
    char* dl_bitrate = ((char**)data)[2];
    asprintf(&cmd, "AT+CGEQREQ=%s,%s,%s,%s", "1", traffic_class, ul_bitrate, dl_bitrate);//cid=1, only for engineer mode
    err = at_send_command(cmd, NULL);
    free(cmd);

    ALOGD("Set3GOS,err = %d\n",err);
    if(err < 0)
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
}
void ril_request_query_3gqos_zte(int request, void *data, size_t datalen, RIL_Token token)
{
	ATResponse *p_response = NULL;
    int response[4];
    char *line = NULL;
    int err = 0;

    err = at_send_command_singleline("AT+CGEQREQ?", "+CGEQREQ:", &p_response);
    if (err < 0 || p_response->success == 0) goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(response[0]));
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(response[1]));
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(response[2]));
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &(response[3]));
    if (err < 0) goto error;

    ALOGD("Query3GOS response=(%d,%d,%d,%d)\n",response[0],response[1],response[2],response[3]);
    if(1 == response[0]) //cid=1, only for engineer mode
    {
        RIL_onRequestComplete(token, RIL_E_SUCCESS, response, sizeof(response));
    }
    else
    {
        ALOGD("%s: Other CID Qos,don't return to ap\n", __FUNCTION__);
    }
	
    at_response_free(p_response);
    return;

    error:
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
        at_response_free(p_response);
}

//added  by liutao 20131203 for TSP 7510 modem and dual standby end
int parseResponseWithMoreInt(ATResponse *response, int responseInt[], int *pNum)
{
	int err = 0, num = 0;
	char *line, *p;
    if (response->success == 0 || response->p_intermediates == NULL)
    {
        goto error;
    }
    line = response->p_intermediates->line;
    err = at_tok_start(&line);
	DO_ERROR_JDUGE;
    while (at_tok_hasmore(&line))
    {
        if (num == 2 || num == 3 || num == 5) //for <lac>,<cid>
        {
            err = at_tok_nexthexint(&line, &(responseInt[num]));
        }
        else
        {
            err = at_tok_nextint(&line, &(responseInt[num]));
        }
		DO_ERROR_JDUGE;
        num++;
    }
    if(num == 2)/* AT Reply format: +CREG: <n>,<stat>[,<lac>,<ci>[,<AcT>]] */
    {
		responseInt[0] = responseInt[1];
		responseInt[1] = responseInt[2] = responseInt[3] = -1;
	}
	else if((num == 4)||(num == 5)) /*4: +CREG: <n>, <stat>, <lac>, <cid> 5: +CREG: <n>, <stat>, <lac>, <cid>, <AcT> */
    {
        responseInt[0] = responseInt[1];
        responseInt[1] = responseInt[2];
        responseInt[2] = responseInt[3];
		responseInt[3] = (num == 5)?responseInt[4]:-1;
    }
	else if(num == 6) /* +CGREG: <n>, <stat>, <lac>, <cid>, <AcT>,<res> */
    {
        responseInt[0] = responseInt[1];
        responseInt[1] = responseInt[2];
        responseInt[2] = responseInt[3];
        responseInt[3] = responseInt[4];
        responseInt[4] = responseInt[5];
    }
	else if(num == 7)
	{
        responseInt[0] = responseInt[1];
        responseInt[1] = responseInt[2];
        responseInt[2] = responseInt[3];
        responseInt[3] = responseInt[4];
        responseInt[4] = responseInt[5];
		responseInt[5] = responseInt[6];
	}
    else
	{
        return -1;
    }
    *pNum = num;
    return 0;
error:
    return err;
}

/* change realCsq to level */
int changeCSQdbmToLevel(int Csq)
{
	ALOGD("xichunyan:enter changeCSQdbmToLevel Csq=%d",Csq);
	if( (Csq <= -113)||(sys_mode == 17)&&(Csq == -42)||(sys_mode == 15)&&(Csq == -17))
	{
		return 0;
	}
	else if ((Csq > -113) && (Csq <= -111))
	{
		return 1;
	}
	/* -109 ~ -53dBm =>  2..30 */
	else if ((Csq > -111) && (Csq <= -53))
	{
		return (0.5 * Csq + 56.5);
	}
	else
	{
		return 31;
	}
}

/* Report to upper layer about signal strength (unsol msg of  +CSQ: ) when screen state is ON */
void reportSignalStrength(void *param)
{
    if (sScreenState)
    {
        /*ZTE_XICHUNYAN_20120808,  change realCsq to level begin*/
        RIL_SignalStrength  result;
        int realCsq = 99;
        memset(&result, 0, sizeof(result));
		
#ifdef CHECK_SYSINFO_ONCE
        //added by Boris 20161125 for no MODE report 
		ALOGD(" %s sOperInfo.act = %d,sys_mode=%d",__FUNCTION__,sOperInfo.act,sys_mode);  
		check_sysinfo();//added by Boris for AT send bug,20161125       
		ALOGD("sys_mode became %d",sys_mode);		
#endif           //gelei  20161209

		
        if (sys_mode == 17 || (7 == sOperInfo.act))//modified by boris for no MODE report .signal lose
        {        
//#ifdef	DIANXIN    //gelei 20161221
#if 1
			if(sCSQ[0] >= 126 && sCSQ[0] < 146 ) //Boris modified 20160922
            {
            	result.LTE_SignalStrength.signalStrength = sCSQ[0] + DIANXIN_WEAKSIGNAL_PLUS_NUM;   //DIANXIN_WEAKSIGNAL_PLUS_NUM = 15
            	result.LTE_SignalStrength.rsrp = 241 - sCSQ[0] - DIANXIN_WEAKSIGNAL_PLUS_NUM;
            }
            else if(sCSQ[0] >= 146 && sCSQ[0] < 156 )
            {
            	result.LTE_SignalStrength.signalStrength = sCSQ[0] + DIANXIN_MIDSIGNAL_PLUS_NUM;   //bufantong   DIANXIN_MIDSIGNAL_PLUS_NUM = 10
            	result.LTE_SignalStrength.rsrp = 241 - sCSQ[0] - DIANXIN_MIDSIGNAL_PLUS_NUM;
            }
            else
            {
            	result.LTE_SignalStrength.signalStrength = sCSQ[0];
            	result.LTE_SignalStrength.rsrp = 241 - sCSQ[0];
            }
#else
			result.LTE_SignalStrength.signalStrength = sCSQ[0];
            result.LTE_SignalStrength.rsrp = 241 - sCSQ[0];
#endif            
            result.LTE_SignalStrength.rsrq = -1;
            result.LTE_SignalStrength.rssnr = 0x7FFFFFFF;
            result.LTE_SignalStrength.cqi = -1;
        }
        else
        {
            if (sCSQ[0] > 99 && sCSQ[0] < 200)
            {
                realCsq = sCSQ[0] - 216;
                result.GW_SignalStrength.signalStrength = changeCSQdbmToLevel(realCsq);
            }
            else
            {
                result.GW_SignalStrength.signalStrength = sCSQ[0];
            }

            ALOGD("xichunyan:reportSignalStrength result.GW_SignalStrength.signalStrength = %d",
                  result.GW_SignalStrength.signalStrength);

            result.GW_SignalStrength.bitErrorRate = sCSQ[1];

            /*ZTE_XICHUNYAN_20120724, begin set LTE signal strength  to match framework/base/telephony/java/android/telephony/SignalStrength.java*/
            result.LTE_SignalStrength.signalStrength = -1;
            result.LTE_SignalStrength.cqi = -1;
            result.LTE_SignalStrength.rsrp = -1;
            result.LTE_SignalStrength.rsrq = -1;
            result.LTE_SignalStrength.rssnr = 0x7FFFFFFF;
            /*ZTE_XICHUNYAN_20120724, begin */
        }
        ALOGD("xichunyan:reportSignalStrength result.LTE_SignalStrength.signalStrength = %d",
              result.LTE_SignalStrength.signalStrength);

        RIL_onUnsolicitedResponse(RIL_UNSOL_SIGNAL_STRENGTH, &result, sizeof(result));
    }
}

/* Reset all local saved reginfo and operInfo to NULL, force to update by AT cmd */
void resetLocalRegInfo(void)
{
    sOperInfo.mode = -1;
    sOperInfo.operLongStr[0] = '\0';
    sOperInfo.operShortStr[0] = '\0';
    sOperInfo.operNumStr[0] = '\0';
    sOperInfo.act = -1;
    sCregState.stat = -1;
    sCgregState.stat = -1;
	sys_mode = -1; //bufantong 20170112
}

void setRadioStateOff()
{
	resetLocalRegInfo();
	setRadioState(RADIO_STATE_OFF);
}

/*** Note:  the screen keeps off when the new sms report, 
       so set the Modem to standby after sending the sms ack, ***/
void setCPstandby(void)
{
    if (!sScreenState)//notify CP that AP will sleep
    {
        ALOGD("xichunyan:AT+ZPOWERIND begin ");
        at_send_command("AT+ZPOWERIND", NULL);
    }
    return;
}


static inline void setScreenState(int state)
{
    __atomic_swap(state, &sScreenState);
}

/* Set flag whether permit to report CSQ or CREG/CGREG ind msg to RIL */
void setNetworkStateReportOption(int flag)
{
    setScreenState(flag);
}

/* Get registe state: return 1: registered, 0: unregistered */
int isRegistered(void)
{
    int regState;

    if ((sCregState.stat == 1) || (sCregState.stat == 5) || (sCgregState.stat == 1) || (sCgregState.stat == 5))
    {
        regState = 1;
    }
    else
    {
        regState = 0;
    }
    return regState;
}

/* Save latest reg status locally */
void saveLatestRegStatus(int stat, int lac, int cid, int isCreg)
{
    if(isCreg == 1)
    {
        sCregState.stat = stat;
        sCregState.lac = lac;
        sCregState.cid = cid;
    }
	else if(isCreg == 0)
    {
        sCgregState.stat = stat;
        sCgregState.lac = lac;
        sCgregState.cid = cid;
    }
}

/* Update Local Reg Info, if reg info changed, report network change unsol msg to upper layer  */
void updateLocalRegInfo(void *param)
{
    ATResponse  *response = NULL;
	int responseInt[10], err, num, oldRegState = sCregState.stat;
    static int  query_times = 0;

	//ALOGD(" ==== xxx _creg <bufantong> /n");
    err = at_send_command_singleline("AT+CREG?", "+CREG:", &response);
	DO_MM_RESPONSE_ERROR_JDUGE;
    err = parseResponseWithMoreInt(response, responseInt, &num);
	DO_ERROR_JDUGE;
	//Register state extention: 13 - Same as 3, but indicates that emergency calls are enabled
	responseInt[0] = (responseInt[0] == 3)?13:responseInt[0];
	//Register state extention: 10 - Same as 0, but indicates that emergency calls are enabled
	responseInt[0] = (responseInt[0] > 5)?10:responseInt[0];
	saveLatestRegStatus(responseInt[0], responseInt[1], responseInt[2], 1);
    at_response_free(response);
    response = NULL;

    err = at_send_command_singleline("AT+CGREG?", "+CGREG:", &response);
	DO_MM_RESPONSE_ERROR_JDUGE;
    err = parseResponseWithMoreInt(response, responseInt, &num);
	DO_ERROR_JDUGE;
	saveLatestRegStatus(responseInt[0], responseInt[1], responseInt[2], 0);
	sOperInfo.act = (num == 5)?responseInt[3]:sOperInfo.act;
    if (oldRegState != sCregState.stat)
    {
        RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED, NULL, 0);
        query_times = 0;
    }
    else
    {
        query_times++;
        if ((query_times < 10) && (!isRegistered()))
        {
			const struct timeval TIMEVAL_15s = { 15, 0 };
            enqueDelayed(getWorkQueue(SERVICE_MM), updateLocalRegInfo, NULL, &TIMEVAL_15s);
        }
        else
        {
            query_times = 0;
        }
    }
    at_response_free(response);
    return;
error:
    sCregState.stat = -1;
    sCgregState.stat = -1;
    ALOGE("%s: Error in sending this AT response", __FUNCTION__);
    at_response_free(response);
    return;
}

/* Convert AcT value in AT cmd to RIL_RadioTechnology value defined in ril.h
 *  Input para: AcT (3GPP spec definition):
 *      0   GSM
 *      1   GSM Compact
 *      2   UTRAN
 *      3   GSM w/EGPRS (see NOTE 1)
 *      4   UTRAN w/HSDPA (see NOTE 2)
 *      5   UTRAN w/HSUPA (see NOTE 2)
 *      6   UTRAN w/HSDPA and HSUPA (see NOTE 2)
 *  Output para: state (Refer to ril.h, RIL_RadioTechnology)
 *   RADIO_TECH_UNKNOWN = 0,
 *   RADIO_TECH_GPRS = 1,
 *   RADIO_TECH_EDGE = 2,
 *   RADIO_TECH_UMTS = 3,
 *   RADIO_TECH_IS95A = 4,
 *   RADIO_TECH_IS95B = 5,
 *   RADIO_TECH_1xRTT =  6,
 *   RADIO_TECH_EVDO_0 = 7,
 *   RADIO_TECH_EVDO_A = 8,
 *   RADIO_TECH_HSDPA = 9,
 *   RADIO_TECH_HSUPA = 10,
 *   RADIO_TECH_HSPA = 11,
 *   RADIO_TECH_EVDO_B = 12,
 *   RADIO_TECH_EHRPD = 13,
 *   RADIO_TECH_LTE = 14,
 *   RADIO_TECH_HSPAP = 15 // HSPA+
 */

static void libConvertActToRilState(int AcT, char *state)
{
    switch (AcT)
    {
        case 0:
        case 1:
        sprintf(state, "%d", RADIO_TECH_GPRS);
        break;

        case 2:
        sprintf(state, "%d", RADIO_TECH_UMTS);
        break;

        case 3:
        sprintf(state, "%d", RADIO_TECH_EDGE);
        break;

        case 4:
        sprintf(state, "%d", RADIO_TECH_HSDPA);
        break;

        case 5:
        sprintf(state, "%d", RADIO_TECH_HSUPA);
        break;

        case 6:
        sprintf(state, "%d", RADIO_TECH_HSPA);
        break;
        /*LTE*/
        case 7:
        sprintf(state, "%d", RADIO_TECH_LTE);
        break;

        case -1:
        default:
        sprintf(state, "%d", RADIO_TECH_UNKNOWN);
        break;
    }

    return;
}


//added by LiaoBin 20161125 for net search
//static pthread_t s_tid_sysinfo;
//extern void *check_sysinfo(void *arg);//deleted by Boris 20161125 for AT send bug.

/* Process RIL_REQUEST_RADIO_POWER */
void ril_request_radio_power(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(data);
    UNUSED(token);
    int onOff = ((int   *) data)[0], err, status;
    ATResponse  *response = NULL;
    RIL_RadioState  currentState = getRadioState();
    //int result = -1;
	
    ALOGD("ril_request_radio_power(): onOff=%d, currentState=%d", onOff, currentState);
    if ((onOff == 4) && (currentState != RADIO_STATE_OFF))
    {
    	 ALOGD("onOff=%d, currentState=%d   --> AT+CFUN=4", onOff, currentState);
        err = at_send_command_timeout("AT+CFUN=4", &response, TIMEOUT_CFUN);
	 // result = 4;
		DO_MM_RESPONSE_ERROR_JDUGE;
        setRadioStateOff();
        airPlaneMode = 1;
		//property_set(ZTERILD_PDP_CGDCONT, "0"); 
       // RIL_onUnsolicitedResponse(RIL_UNSOL_SET_CS_AIRPLANE, &result, sizeof(result));
    }
    else if ((onOff == 0) && (currentState != RADIO_STATE_OFF))
    {
    	 ALOGD("onOff=%d, currentState=%d   --> AT+CFUN=0", onOff, currentState);
        err = at_send_command_timeout("AT+CFUN=0", &response, TIMEOUT_CFUN);
	  //result = 0;
		DO_MM_RESPONSE_ERROR_JDUGE;
        err = at_send_command("AT^ZPOWEROFF", NULL);
        ALOGD("xichunyan:AT^ZPOWEROFF:   err = %d", err);
        setRadioStateOff();
    }
    else if (((onOff == 1) && (currentState == RADIO_STATE_OFF)) || (currentState == RADIO_STATE_UNAVAILABLE))
    {
    	
    	#if 0                                    //added by Boris20161021 for iccid unsolited early  
    	ALOGD("at+ziccid? send for getlccSeriaNumber");
    	err = at_send_command_timeout("AT+ZICCID?",&response,TIMEOUT_CFUN);
    	if((err < 0 || response -> success == 0)&&(isRadioOn() != 1))
    		{
    			goto error;
    		}
    	#endif
    	
    	
    	 ALOGD("onOff=%d, currentState=%d   --> AT+CFUN=1", onOff, currentState);
        err = at_send_command_timeout("AT+CFUN=1", &response, TIMEOUT_CFUN);
	  //result = 1;
        if ((err < 0 || response->success == 0)&&(isRadioOn() != 1))
        {
            goto error;
        }
       airPlaneMode = 0;
        updateRadioState();
        
        
#if 0 //added by LiaoBin 20161125 for sys_info net search,delted by Boris,20161125 for bug
    {
        int ret = 0;
        pthread_t tid;
        pthread_attr_t attr;
 
        RLOGD( "RIL_Init check sys_info");
        pthread_attr_init (&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        ret = pthread_create(&s_tid_sysinfo, &attr, check_sysinfo, &attr);
        if (ret < 0) {
            RLOGD( "RIL_Init check sys_info thread fail");
            perror ("pthread_create");
        }
    }
#endif
        
        //RIL_onUnsolicitedResponse(RIL_UNSOL_SET_CS_AIRPLANE, &result, sizeof(result));
    }
    else if ((onOff == 4) && (airPlaneMode == 1))
    {
    	ALOGD("onOff=%d, airPlaneMode=%d   --> AT^ZPOWEROFF", onOff, airPlaneMode);
        at_send_command("AT^ZPOWEROFF", NULL);
        setRadioStateOff();
    }
    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
    goto exit;
error:
   // RIL_onUnsolicitedResponse(RIL_UNSOL_SET_CS_AIRPLANE, &result, sizeof(result));
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    updateRadioState();//if at error, we must update radio state
exit:
    at_response_free(response);
}

void ril_request_shut_down_radio(int request, void *data, size_t datalen, RIL_Token token)
{

	ATResponse	*response = NULL;
	int err;
		
	err = at_send_command_timeout("AT+CFUN=0", &response, TIMEOUT_CFUN);
	
	if (err < 0 || response->success == 0)
	{
		goto error;
	}
	setRadioStateOff();	
	RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	goto exit;
	
	error:
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	exit:
	at_response_free(response);


	

}

static void pollModemNeedPinSwitch()
{
	char modemNeedPin[PROPERTY_VALUE_MAX] = {0};
    while(1)
	{
		if ( 0 == property_get("ril.modemNeedPin", modemNeedPin, "0")) 
		{
			ALOGI("pollModemNeedPinSwitch get modemNeedReset switch failed\n");
			return;
		} 
		else 
		{
			ALOGI("pollModemNeedPinSwitch get modemNeedReset switch: %s \n",modemNeedPin); 
			if (0 == strcmp(modemNeedPin, "1"))
			{
				sleep(1);
			}
			else
			{
				return;
			}
		}
	}
	return;
}

void ril_request_radio_power_hardreset(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);
	
    ALOGD("ril_request_radio_power_hardreset");
	//pollModemNeedPinSwitch();
    setRadioStateOff();
    reset_zte_ril = 1;
    at_send_command("AT", NULL);
    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
}

/* Process RIL_REQUEST_RESET_RADIO */
void ril_request_reset_radio(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

	int onOff, err;
    ATResponse  *p_response = NULL;

    err = at_send_command_timeout("AT+CFUN=4", &p_response, TIMEOUT_CFUN);
    if (err < 0 || p_response->success == 0)
    {
        goto error;
    }

    setRadioStateOff();

    err = at_send_command_timeout("AT+CFUN=1", &p_response, TIMEOUT_CFUN);
    if (err < 0 || p_response->success == 0)
    {
        /* Some stacks return an error when there is no SIM, but they really turn the RF portion on
             * So, if we get an error, let's check to see if it turned on anyway
             */
        if (isRadioOn() != 1)
        {
            goto error;
        }
    }

    /* start to search nw */
    at_send_command_timeout("AT+COPS=0", NULL, TIMEOUT_COPS);

    updateRadioState();

    at_response_free(p_response);
    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
    return;

    error:
    at_response_free(p_response);
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    return;
}

void ril_request_screen_state(int request, void *data, size_t datalen, RIL_Token token)
{
//#define UNUSED(a)    (void)(a)

    UNUSED(request);
    UNUSED(datalen);

    setScreenState(((int *) data)[0]);
    if (getRadioState() != RADIO_STATE_UNAVAILABLE)
    {
        if (!sScreenState)//notify CP that AP will sleep
        {
            ALOGD("xichunyan:AT+ZPOWERIND begin ");
            at_send_command("AT+ZPOWERIND", NULL);
        }
        else
        {
            /*** Note:  when screen on, update +CREG state***/
            RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED, NULL, 0);
        }
    }
    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
}

/**
 * RIL_REQUEST_VOICE_REGISTRATION_STATE
 *
 * Request current voice registration state.
 */
/* ril\C7\EB\C7\F3\C9\F9\D2\F4ע\B2\E1״̬ */
void ril_request_voice_registration_state(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);
    ATResponse  *response = NULL;
    int responseInt[10], num, err;
    //char    *responseStr[15] = {NULL}, radiotech[3];   //gelei 20170311
    char    *responseStr[4] = {NULL}, radiotech[3];   //gelei 20170311
    const char  *cmd, *prefix;
    err = at_send_command_singleline("AT+CREG?", "+CREG:", &response);    //GELEI 20161221
    DO_MM_RESPONSE_ERROR_JDUGE;  //gelie 20170311
    
    if(sys_mode == 17)   //GELEI 20161221
    {
    	err = at_send_command_singleline("AT+CEREG?", "+CEREG:", &response);
    }
    else
    {
    	//ALOGD(" ==== _creg <bufantong> /n");
    	err = at_send_command_singleline("AT+CREG?", "+CREG:", &response);
    }
    	
	DO_MM_RESPONSE_ERROR_JDUGE;  //gelei 20170311
	/* \BD\E2\CE\F6\CF\E0Ӧ */
	
    err = parseResponseWithMoreInt(response, responseInt, &num);
	DO_ERROR_JDUGE;
	
    if (responseInt[0] > 5)
    {
        responseInt[0] = 10;
    } //Register state extention: 10 - Same as 0, but indicates that emergency calls are enabled
    else if (responseInt[0] == 3)
    {
        responseInt[0] = 13;
    } //Register state extention: 13 - Same as 3, but indicates that emergency calls are enabled
    asprintf(&responseStr[0], "%d", responseInt[0]);
    if (num > 2)
    {
        asprintf(&responseStr[1], "%x", responseInt[1]);
        asprintf(&responseStr[2], "%x", responseInt[2]);
    }
    else
    {
        responseStr[1] = NULL;
        responseStr[2] = NULL;
    }
    if (num >= 5)
    {
        libConvertActToRilState(responseInt[3], radiotech);
        responseStr[3] = radiotech;
    }
    else
    {
        responseStr[3] = NULL;
    }
	/* \C7\EB\C7\F3 */
    RIL_onRequestComplete(token, RIL_E_SUCCESS, responseStr, sizeof(responseStr));
    FREE(responseStr[0]);
    FREE(responseStr[1]);
    FREE(responseStr[2]);
	/* \B1\A3\B4\E6\D7\EE\BD\FC\B5\C4״̬ */
	saveLatestRegStatus(responseInt[0], responseInt[1], responseInt[2], 1);
	sOperInfo.act = (num == 5)?responseInt[3]:sOperInfo.act;
    goto exit;
error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
    at_response_free(response);
	setCPstandby();
}

void ril_request_data_registration_state(int request, void *data, size_t datalen, RIL_Token token)
{
	UNUSED(request);
	UNUSED(data);
	UNUSED(datalen);

	ATResponse* response = NULL;
	int responseInt[10];
	//char *responseStr[7] = {NULL}, gprsState[3];   //gelei 20170311
	char *responseStr[4] = {NULL}, gprsState[3]; //gelei 20170311
	const char *cmd;
	const char *prefix;
	int num, err;

	if (sys_mode == 17)
    {
        err = at_send_command_singleline("AT+CEREG?", "+CEREG:", &response);
    }
    else
    {
        err = at_send_command_singleline("AT+CGREG?", "+CGREG:", &response);
    }    
	if (err < 0 || response->success == 0)
        	goto error;
        
	err = parseResponseWithMoreInt(response, responseInt, &num);
	if (err < 0) goto error;

	asprintf(&responseStr[0], "%d", responseInt[0]);
	if(num > 2)
	{
	       asprintf(&responseStr[1], "%x", responseInt[1]);
	       asprintf(&responseStr[2], "%x", responseInt[2]);
	}
	else
	{
	       responseStr[1] = NULL;
	       responseStr[2] = NULL;
	}

	if ((num == 5)||(num == 6)||(num == 7))
	{
	       /* Convert AcT value of 3GPP spec to GPRS reg state defined in ril.h */
	       libConvertActToRilState(responseInt[3], gprsState);
	       responseStr[3] = gprsState;
	}
	else
	{      
	      responseStr[3] = NULL;
	}
	/*
	responseStr[4] = NULL; // FIXME
	asprintf(&responseStr[5], "%d", max_support_data_call_count());
    asprintf(&responseStr[6], "%d", responseInt[5]);
    */  //gelei 20170311
	RIL_onRequestComplete(token, RIL_E_SUCCESS, responseStr, sizeof(responseStr));
	FREE(responseStr[0]);
	FREE(responseStr[1]);
	FREE(responseStr[2]);
	/*
	FREE(responseStr[5]);
	FREE(responseStr[6]);
	*/  //gelei 20170311

	/* Save latest reg status locally */
	sCgregState.stat = responseInt[0];
	sCgregState.lac	= responseInt[1];
	sCgregState.cid	= responseInt[2];
	if (num >= 5)
	       sOperInfo.act = responseInt[3];
	goto exit;

error:
    ALOGE("%s: Format error in this AT response", __FUNCTION__);
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
    at_response_free(response);
	setCPstandby();
}

void responseOperator(RIL_Token token, char *result0, char *result1, char *result2)
{
	char	*result[3];
	result[0] = result0;
	result[1] = result1;
	result[2] = result2;
	RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(result));
	return;
}

int responseOperatorWithoutQuery(RIL_Token token)
{
    /*if (!isRegistered())
    {
		responseOperator(token, "", "", "");
        ALOGD("%s: unregistered, return empty info", __FUNCTION__);
        return 1;
    }*/
    if ((sOperInfo.operLongStr[0] != '\0') && (sOperInfo.operShortStr[0] != '\0') && (sOperInfo.operNumStr[0] != '\0'))
    {
	    responseOperator(token, &(sOperInfo.operLongStr[0]), &(sOperInfo.operShortStr[0]), &(sOperInfo.operNumStr[0]));
        ALOGD("%s: Return local saved operator info", __FUNCTION__);
        return 1;
    }
	return 0;
}
/**
 * RIL_REQUEST_OPERATOR
 *
 * Request current operator ONS or EONS.
 */


void ril_request_operator(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);
    if (1 == responseOperatorWithoutQuery(token))
    {
        return;
    }
    ATResponse  *response = NULL;
    int err, i, skip;
    ATLine  *p_cur;
    char    *result[3] = {0};
    err = at_send_command_multiline(
            "AT+COPS=3,0;+COPS?;+COPS=3,1;+COPS?;+COPS=3,2;+COPS?",
            "+COPS:", &response);
	DO_MM_RESPONSE_ERROR_JDUGE;
    if (strStartsWith(response->finalResponse, "+CME ERROR:") || response->p_intermediates == NULL)
    {
        goto error;
    }
    for (i = 0, p_cur = response->p_intermediates
            ; p_cur != NULL
            ; p_cur = p_cur->p_next, i++
        ) {
        char *line = p_cur->line;

        err = at_tok_start(&line);
        if (err < 0) goto error;

        err = at_tok_nextint(&line, &skip);
        if (err < 0) goto error;
        sOperInfo.mode = skip;
        
        // If we're unregistered, we may just get
        // a "+COPS: 0" response
        if (!at_tok_hasmore(&line)) {
            result[i] = NULL;
            continue;
        }

        err = at_tok_nextint(&line, &skip);
        if (err < 0) goto error;

        // a "+COPS: 0, n" response is also possible
        if (!at_tok_hasmore(&line)) {
            result[i] = NULL;
            continue;
        }

        err = at_tok_nextstr(&line, &(result[i]));
        if (err < 0) goto error;

        if (!at_tok_hasmore(&line)) {
            result[i] = NULL;
            continue;
        }

        err = at_tok_nextint(&line, &skip);
        if (err < 0) goto error;
        
        if(i == 2)    /* i == 2 means strings of the 3ed resp  */
        {
            if((strcmp(result[i],"46000") == 0) \
                || (strcmp(result[i],"46002") == 0)\
                || (strcmp(result[i],"46007") == 0))
            {
                if(skip == 2) {
                /* skip == 2 means TD network, the 4th para */
                    result[0] = "CHINA MOBILE 3G";
                /*we want to change the first resp */
                }
                else if(skip == 7){
                    result[0] = "CHINA MOBILE 4G";
                }
                else {
                    result[0] = "CHINA MOBILE";
                }
            }
            else if(strcmp(result[i],"46001") == 0) {
                //RLOGD("===>THIS is china unicom.\n");
                result[0] = "CHINA UNICOM";
                /*Support the operator of china unicom*/
            }else if(strcmp(result[i],"46011") == 0) {
                RLOGD("===>THIS is china telecom.\n");
                result[0] = "CHINA TELECOM";
                /*Support the operator of china unicom*/
            }else if(strlen(result[0]) == 0) {
                //add by guojing for Enh00000638
                result[0] = result[i];
                //add end
            }
        }
        RLOGD("===> requestOperator,%s,skip = %d, i =%d\n",result[i],skip,i);
    }
    if(result[2] != NULL)
    {
    	strcpy(sOperInfo.operNumStr,result[2]);
    }
    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(result));
    /*When operator name is available, the network should be registered.
    * If CP doesn't send indication msg, we need to query CP and update reg info */
    goto exit ;
error : ALOGE("%s: Error in this AT response", __FUNCTION__);
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit : at_response_free(response);
        setCPstandby();

}


/**
 * RIL_REQUEST_QUERY_NETWORK_SELECTION_MODE
 *
 * Query current network selectin mode.
 */
void ril_request_query_network_selection_mode(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ATResponse  *response = NULL;
    int err, result[1] = {0};
    char    *line;

    err = at_send_command_singleline("AT+COPS?", "+COPS:", &response);
    if (err < 0 || response->success == 0 || response->p_intermediates == NULL)
    {
        goto error;
    }

    line = response->p_intermediates->line;

    err = at_tok_start(&line);

    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextint(&line, &result[0]);

    if (err < 0)
    {
        goto error;
    }

    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(int));

    sOperInfo.mode = result[0];
    goto exit;
    error:
    ALOGE("%s: Respond error, return default value 0: auto selection", __FUNCTION__);
    result[0] = 0;
    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(int));
    exit:
    at_response_free(response);
	setCPstandby();
}
static int check_sysinfo_(  )
{
	ATResponse  *response = NULL;
	int sysinfo[4];
	int err, acqorder;
	int srv_status = 0;
	int srv_domain = 0;
	int roam_status = 0;
	char    *line;

	err = at_send_command_singleline("AT^SYSINFO", "^SYSINFO:", &response);
	if (err < 0)
	return 0;
    line = response->p_intermediates->line;
    err = at_tok_start(&line);
	if (err < 0)
		return 0;
    err = at_tok_nextint(&line, &(sysinfo[0]));
	if (err < 0)
		return 0;
    err = at_tok_nextint(&line, &(sysinfo[1]));
	if (err < 0)
		return 0;
    err = at_tok_nextint(&line, &(sysinfo[2])); //漫游、非漫游
	if (err < 0)
		return 0;
    err = at_tok_nextint(&line, &sys_mode); //17-4g 3-2g  5、15 - 3g
	if (err < 0)
		return 0;

#if 0
	err = at_send_command_singleline("AT^SYSCONFIG?", "^SYSCONFIG:", &response);
	line = response->p_intermediates->line;
    err = at_tok_start(&line);
	if (err < 0)
		return 0;
    err = at_tok_nextint(&line, sysmod);
	if (err < 0)
		return 0;
    err = at_tok_nextint(&line, &acqorder);
	if (err < 0)
		return 0;
#endif

	return 1;
}
void check_cereg_creg_(void)
{
    ATResponse  *response = NULL;
    int responseInt[10], num, err;
    char    *responseStr[15] = {NULL}, radiotech[3];
    const char  *cmd, *prefix;

	check_sysinfo_();
	ALOGD("%s,sys_mode became %d",__FUNCTION__,sys_mode);
	if(sys_mode == 17) {
        err = at_send_command_singleline("AT+CEREG?", "+CEREG:", &response);
	}
    else{
        err = at_send_command_singleline("AT+CREG?", "+CREG:", &response);
    }
	DO_MM_RESPONSE_ERROR_JDUGE;
    err = parseResponseWithMoreInt(response, responseInt, &num);
	DO_ERROR_JDUGE;
    if (responseInt[0] > 5)
    {
        responseInt[0] = 10;
    } //Register state extention: 10 - Same as 0, but indicates that emergency calls are enabled
    else if (responseInt[0] == 3)
    {
        responseInt[0] = 13;
    } //Register state extention: 13 - Same as 3, but indicates that emergency calls are enabled
    asprintf(&responseStr[0], "%d", responseInt[0]);
    if (num > 2)
    {
        asprintf(&responseStr[1], "%x", responseInt[1]);
        asprintf(&responseStr[2], "%x", responseInt[2]);
    }
    else
    {
        responseStr[1] = NULL;
        responseStr[2] = NULL;
    }
    if (num >= 5)
    {
        libConvertActToRilState(responseInt[3], radiotech);
        responseStr[3] = radiotech;
    }
    else
    {
        responseStr[3] = NULL;
    }
    FREE(responseStr[0]);
    FREE(responseStr[1]);
    FREE(responseStr[2]);
	saveLatestRegStatus(responseInt[0], responseInt[1], responseInt[2], 1);
	ALOGD("%s,num=%d,sOperInfo.act=%d",__FUNCTION__,num,sOperInfo.act);
	sOperInfo.act = (num >= 5)?responseInt[3]:sOperInfo.act;
    goto exit;
error:
exit:
    at_response_free(response);
}

static int check_creg(void)
{
	ALOGD(" ==== check_creg <bufantong> ");
	int responseInt[10], err, num = 0;
	ATResponse  *response = NULL;
	//int sysinfo[4] = {0}; //bufantong 20170110
	char *line;
	ALOGD(" ==== sys_mode = %d <bufantong> ",sys_mode);
	//bft 20170110
	if(sys_mode == 17) {
    	ALOGD(" ==== 111 <bufantong> ");
    	err = at_send_command_singleline("AT+CEREG?", "+CEREG:", &response);
		line = response->p_intermediates->line;
	    err = at_tok_start(&line);
		if (err < 0) return 0;
		err = at_tok_nextint(&line, &(responseInt[0]));
		if (err < 0) return 0;
		err = at_tok_nextint(&line, &(responseInt[1]));
		if (err < 0) return 0;
    } else {
    	ALOGD(" ==== 222 <bufantong> ");
    	err = at_send_command_singleline("AT+CREG?", "+CREG:", &response);
		line = response->p_intermediates->line;
	    err = at_tok_start(&line);
		if (err < 0) return 0;
	    while (at_tok_hasmore(&line))
	    {
	        if (num == 2 || num == 3 || num == 5) //for <lac>,<cid>
	        {
	            err = at_tok_nexthexint(&line, &(responseInt[num]));
	        } else {
	            err = at_tok_nextint(&line, &(responseInt[num]));
	        }
			if (err < 0) return 0;
	        num++;
	    }
	    if(num == 2)/* AT Reply format: +CREG: <n>,<stat>[,<lac>,<ci>[,<AcT>]] */
	    {
			responseInt[0] = responseInt[1];
			responseInt[1] = responseInt[2] = responseInt[3] = -1;
		}
		else if((num == 4)||(num == 5)) /*4: +CREG: <n>, <stat>, <lac>, <cid> 5: +CREG: <n>, <stat>, <lac>, <cid>, <AcT> */
	    {
	        responseInt[0] = responseInt[1];
	        responseInt[1] = responseInt[2];
	        responseInt[2] = responseInt[3];
			responseInt[3] = (num == 5)?responseInt[4]:-1;
	    }
		else if(num == 6) /* +CGREG: <n>, <stat>, <lac>, <cid>, <AcT>,<res> */
	    {
	        responseInt[0] = responseInt[1];
	        responseInt[1] = responseInt[2];
	        responseInt[2] = responseInt[3];
	        responseInt[3] = responseInt[4];
	        responseInt[4] = responseInt[5];
	    }
		else if(num == 7)
		{
	        responseInt[0] = responseInt[1];
	        responseInt[1] = responseInt[2];
	        responseInt[2] = responseInt[3];
	        responseInt[3] = responseInt[4];
	        responseInt[4] = responseInt[5];
			responseInt[5] = responseInt[6];
		} else {
	        return -1;
	    }
	}
	
	ALOGD(" ====  <bufantong> responseInt =%d",responseInt[1]);
	//err = parseResponseWithMoreInt(response, responseInt, &num);
	ALOGD(" ==== 333 <bufantong> ");

	#if 1
	if (responseInt[1] != 1 && responseInt[1] != 5 ) //1\D2\D1ע\B2\E1 5ע\B2\E1\C2\FE\D3\CE
    {
    	ALOGD(" ==== 444 <bufantong> ");
        system("echo 0 > /sys/class/lc4g_ctrl/lc4g_ctrl/enable");//bufantong 20170110
		sleep(2);
		system("echo 1 > /sys/class/lc4g_ctrl/lc4g_ctrl/enable");

		at_close_fds();
		//find_available_dev();
		kill(getpid(), 10);
    }
	ALOGD(" ==== 555 <bufantong> ");
	#endif
	
	return 1;
}


/**
 * RIL_REQUEST_SIGNAL_STRENGTH
 *
 * Requests current signal strength and bit error rate.
 *
 * Must succeed if radio is on.
 */
void ril_request_signal_strength(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);
    ATResponse  *response = NULL;
    int err, realCsq = 99, tmpRssi;
    RIL_SignalStrength  result;
    char    *line;
    memset(&result, 0, sizeof(result));
    
 #ifdef CHECK_SYSINFO_ONCE
	//added by Boris 20161205 for no MODE report 
	ALOGD(" %s sOperInfo.act = %d,sys_mode=%d",__FUNCTION__,sOperInfo.act,sys_mode);
	check_sysinfo();//added by Boris 20161205
	ALOGD("sys_mode became %d",sys_mode);
	//if (sys_mode == 17 || (7 == sOperInfo.act))//modified by boris for no MODE report .signal lose
 #endif    //gelei  20161209

    err = at_send_command_singleline("AT+CSQ", "+CSQ:", &response);
	DO_MM_RESPONSE_ERROR_JDUGE;
    line = response->p_intermediates->line;
    err = at_tok_start(&line);
	DO_ERROR_JDUGE;
    err = at_tok_nextint(&line, &tmpRssi);
	DO_ERROR_JDUGE;
    err = at_tok_nextint(&line, &result.GW_SignalStrength.bitErrorRate);
	DO_ERROR_JDUGE;

//#if 1
#ifdef CHECK_CREG_CEREG
	check_cereg_creg_();
	if ( sOperInfo.act == -1 && sys_mode != -1 && sys_mode != 0 ) { //ǰ\CC\E1\CA\C7\D3\D0\D0ź\C5
		//if ( check_creg_crerg_ == 0 ) {
			ALOGD(" ==== 444 <bufantong> sOperInfo.act = %d sys_mode = %d",sOperInfo.act,sys_mode);
	        system("echo 0 > /sys/class/lc4g_ctrl/lc4g_ctrl/enable");//bufantong 20170110
			sleep(2);
			system("echo 1 > /sys/class/lc4g_ctrl/lc4g_ctrl/enable");
			at_close_fds();
			kill(getpid(), 10);	
		//}
		//check_creg_crerg_ = 1;
	}
#endif
	
    if (sys_mode == 17)/*LTE*/
    {
//#ifdef DIANXIN   //gelei 20161221
#if 1
			if(tmpRssi >= 126 && tmpRssi < 146 ) //Boris modified 20160922
            {
            	 realCsq = tmpRssi - 241 + DIANXIN_WEAKSIGNAL_PLUS_NUM;
            }
            else if(tmpRssi >= 146 && tmpRssi < 156 )
            {
            	realCsq = tmpRssi - 241 + DIANXIN_MIDSIGNAL_PLUS_NUM;            	  //bufantong   DIANXIN_MIDSIGNAL_PLUS_NUM = 10
            }
            else
            {
							realCsq = tmpRssi - 241;
            }
#else
		realCsq = tmpRssi - 241;
#endif
        result.LTE_SignalStrength.signalStrength = changeCSQdbmToLevel(realCsq);
        
//#ifdef DIANXIN       //gelei  20161221
#if 1
        		if(tmpRssi >= 126 && tmpRssi < 146 ) //Boris modified 20160922
            {
            	 result.LTE_SignalStrength.rsrp = tmpRssi - 241 + DIANXIN_WEAKSIGNAL_PLUS_NUM;
            }
            else if(tmpRssi >= 146 && tmpRssi < 156 )
            {
            	 result.LTE_SignalStrength.rsrp = tmpRssi - 241 + DIANXIN_MIDSIGNAL_PLUS_NUM;   //bufantong   DIANXIN_MIDSIGNAL_PLUS_NUM = 10
            }
            else
            {
							 result.LTE_SignalStrength.rsrp = tmpRssi - 241;
            }
#else
		result.LTE_SignalStrength.rsrp =tmpRssi - 241;
#endif        
        result.LTE_SignalStrength.rsrq = -1;
        result.LTE_SignalStrength.rssnr = 0x7FFFFFFF;
        result.LTE_SignalStrength.cqi = -1;
    }
    else
    {
        if (tmpRssi > 99 && tmpRssi < 200)
        {
            realCsq = tmpRssi - 216;
            result.GW_SignalStrength.signalStrength = changeCSQdbmToLevel(realCsq);
        }
        else
        {
            result.GW_SignalStrength.signalStrength = tmpRssi;
       }

        ALOGD("xichunyan:ril_request_signal_strength result.GW_SignalStrength.signalStrength = %d",
              result.GW_SignalStrength.signalStrength);
        /*ZTE_XICHUNYAN_20120724, begin set LTE signal strength  to match framework/base/telephony/java/android/telephony/SignalStrength.java*/
        result.LTE_SignalStrength.signalStrength = -1;
        result.LTE_SignalStrength.rsrp = -1;
        result.LTE_SignalStrength.rsrq = -1;
        result.LTE_SignalStrength.rssnr = 0x7FFFFFFF;
        result.LTE_SignalStrength.cqi = -1; 
    }
    RIL_onRequestComplete(token, RIL_E_SUCCESS, &result, sizeof(result));
    goto exit;
error : RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit : at_response_free(response);
        setCPstandby();
}

/**
 * RIL_REQUEST_SET_NETWORK_SELECTION_AUTOMATIC
 *
 * Specify that the network should be selected automatically.
 */
void ril_request_set_network_selection_automatic(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ATResponse  *response = NULL;
    int err;
    int result[2];
    char    *line;

    resetLocalRegInfo();
    err = at_send_command_timeout("AT+COPS=0", &response, TIMEOUT_COPS);
    if (err < 0 || response->success == 0)
    {
        goto error;
    }
    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);

    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED, NULL, 0);

    goto exit;
    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}

/**
 * RIL_REQUEST_SET_NETWORK_SELECTION_MANUAL
 *
 * Manually select a specified network.
 *
 * The radio baseband/RIL implementation will try to camp on the manually
 * selected network regardless of coverage, i.e. there is no fallback to
 * automatic network selection.
 */
void ril_request_set_network_selection_manual(int request, void *data, size_t datalen, RIL_Token token)
	{
		UNUSED(request);
		UNUSED(datalen);
    ATResponse  *response = NULL;
    int err, result[2], act = 0;
	char *line, *act_s = NULL;
	char cmdString[64];
    char *operInfo = ((char **) data)[0];

    
    resetLocalRegInfo();
	 
    if (datalen / sizeof(char *) == 2)
    {
        act_s = ((char **) data)[1];
    }
	if (strcmp(operInfo, sOperInfo.operNumStr) == 0 && NULL == act_s)
    {
        RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
    }
	
    else
    {
        if (act_s != NULL)
        {
            if (strcmp(act_s, "UTRAN") == 0)
            {
                act = 2;
            }
            else if (strcmp(act_s, "EUTRAN") == 0)
            {
                act = 7;
            }
            else if (strcmp(act_s, "GSM COMPACT") == 0)
            {
                act = 1;
            }
            else if (strcmp(act_s, "GSM") == 0)
            {
                act = 0;
			}
			snprintf(cmdString,sizeof(cmdString), "AT+COPS=1,2,\"%s\",%d", operInfo, act);
	}
        else
        {
            snprintf(cmdString,sizeof(cmdString), "AT+COPS=1,2,\"%s\"", operInfo);
        }
    }
    err = at_send_command_timeout(cmdString, &response, TIMEOUT_COPS);
    if (err < 0 || response->success == 0)
    {
        goto error;
    }
    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED, NULL, 0);
    goto exit;
error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
    at_response_free(response);
}

#define AVAILABLE_NETWORKS_ITEM_NUM  5
void getAvailableNetworksResult(int availableOptNumber, char **result, char *line)
{
    int i = 0;
    while (i < availableOptNumber)
    {
		char* status, *longname, *shortname, *numbername, *act;
        at_tok_nextstr(&line, &status);
        switch (status[1])
        {
            case '1':
            result[i * AVAILABLE_NETWORKS_ITEM_NUM + 3] = "available";
            break;
            case '2':
            result[i * AVAILABLE_NETWORKS_ITEM_NUM + 3] = "current";
            break;
            case '3':
            result[i * AVAILABLE_NETWORKS_ITEM_NUM + 3] = "forbidden";
            break;
            default:
            result[i * AVAILABLE_NETWORKS_ITEM_NUM + 3] = "unknown";
        }
        at_tok_nextstr(&line, &result[i * AVAILABLE_NETWORKS_ITEM_NUM]);
        at_tok_nextstr(&line, &result[i * AVAILABLE_NETWORKS_ITEM_NUM + 1]);
        at_tok_nextstr(&line, &result[i * AVAILABLE_NETWORKS_ITEM_NUM + 2]);
        at_tok_nextstr(&line, &act);
    #if (AVAILABLE_NETWORKS_ITEM_NUM == 5)
        switch (act[0])
        {
            case '0':
		    case '3':
			    result[i * AVAILABLE_NETWORKS_ITEM_NUM + 4] = "GSM";
			    break;
		    case '1':
			    result[i * AVAILABLE_NETWORKS_ITEM_NUM + 4] = "GSM_COMPACT";
			    break;
		    case '2':
		    case '4':
		    case '5':
		    case '6':
		    	result[i * AVAILABLE_NETWORKS_ITEM_NUM + 4] = "UTRAN";
			    break;
			case '7':
				result[i * AVAILABLE_NETWORKS_ITEM_NUM + 4] = "EUTRAN";
			    break;
            default:
            result[i * AVAILABLE_NETWORKS_ITEM_NUM + 4] = "UNKNOWN";
        }
    #endif
        if((strcmp(result[i * AVAILABLE_NETWORKS_ITEM_NUM + 4],"UTRAN")== 0)||((strcmp(result[i * AVAILABLE_NETWORKS_ITEM_NUM + 4],"EUTRAN"))== 0))
        {
        	line = line + 3;
        }
        i++;
    }
}
/**
 * RIL_REQUEST_QUERY_AVAILABLE_NETWORKS
 *
 * Scans for available networks.
 */

void ril_request_query_available_networks(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ATResponse  *response = NULL;
	int err, availableOptNumber, lparen = 0, nested = 0;
	char *line, *p, **result;

    err = at_send_command_singleline_timeout("AT+COPS=?", "+COPS:", &response, TIMEOUT_COPS_TEST);
	DO_RESPONSE_ERROR_JDUGE;
    line = response->p_intermediates->line;

    err = at_tok_start(&line);
	DO_ERROR_JDUGE;
    for (p = line; *p != '\0'; p++)/* count number of outter lparen */
    {
        if (*p == '(')
        {
            if (nested == 0)
            {
                lparen++;
            }
            nested++;
        }
        else if (*p == ')')
        {
            nested--;
        }
    }
    /*
     * the response is +COPS:(op1),(op2),...(opn),,(0,1,2,3,4),(0-6)
     * so available operator count should be num_of_left_parentheses - 2
     */
    availableOptNumber = (lparen > 1)?(lparen - 2):0;
    ALOGD("%s: available operator number:%d", __FUNCTION__, availableOptNumber);
    result = alloca(availableOptNumber * AVAILABLE_NETWORKS_ITEM_NUM * sizeof(char *));
	getAvailableNetworksResult(availableOptNumber, result, line);
    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(char *) * availableOptNumber * AVAILABLE_NETWORKS_ITEM_NUM);
    goto exit;
error:
    ALOGE("%s: Format error in this AT response", __FUNCTION__);
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
    at_response_free(response);
}

/**
 * RIL_REQUEST_SET_PREFERRED_NETWORK_TYPE
 *
 * Requests to set the preferred network type for searching and registering
 * (CS/PS domain, RAT, and operation mode).
 */
void ril_request_set_preferred_network_type(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);
    ATResponse  *response = NULL;
	int err = 0, networkType = ((int *)data)[0];
	
//#ifndef LIANTONG       //gelei 20161216
#ifdef ZX_DT_RAT_HANDOVER	 //added by Boris,20160908
	err = at_send_command("AT+CFUN=4",	&response);
	DO_MM_RESPONSE_ERROR_JDUGE;
#endif
    //cm_cu_ct = 0;
	switch (networkType)                   // gelei 20161026    
		{
		#if 1   //bufantong 20161223 17.29
		     case PREF_NET_TYPE_GSM_WCDMA:	    		     
		     {
			 	ALOGI(" PREF_NET_TYPE_GSM_WCDMA cm_cu_ct = %d",cm_cu_ct);
				if ( cm_cu_ct == CTCC ) {
			    	err = at_send_command("AT^SYSCONFIG=17,6,1,2",  &response);
				} else if ( cm_cu_ct == CUCC ) {
					err = at_send_command("AT^SYSCONFIG=2,6,1,2",  &response);		    
				} else if ( cm_cu_ct == CMCC ) {
					err = at_send_command("AT^SYSCONFIG=18,2,1,2",  &response);
				} else {
					err = at_send_command("AT^SYSCONFIG=2,6,1,2",  &response);
				}
				DO_MM_RESPONSE_ERROR_JDUGE;
				break;
		    }
		     case PREF_NET_TYPE_GSM_ONLY: 		    		     
		     {
			 	ALOGI(" PREF_NET_TYPE_GSM_ONLY cm_cu_ct = %d",cm_cu_ct);
				if ( cm_cu_ct == CTCC ) {
			    	err = at_send_command("AT^SYSCONFIG=17,6,1,2",  &response);
				}else if ( cm_cu_ct == CUCC ) {
					err = at_send_command("AT^SYSCONFIG=13,0,1,2",  &response);
				}else if ( cm_cu_ct == CMCC ) {
			    	err = at_send_command("AT^SYSCONFIG=13,0,1,2",  &response);
				} else {
					err = at_send_command("AT^SYSCONFIG=2,6,1,2",  &response);
				}
				DO_MM_RESPONSE_ERROR_JDUGE;
				break;
		    }
		    //case PREF_NET_TYPE_TD_PREF:
		    case PREF_NET_TYPE_LTE_CDMA:
			{
				ALOGI(" PREF_NET_TYPE_LTE_CDMA cm_cu_ct = %d",cm_cu_ct);
				if ( cm_cu_ct == CTCC ) {
			 		err = at_send_command("AT^SYSCONFIG=17,6,1,2",  &response);
			 	}else if ( cm_cu_ct == CUCC ) {
					err = at_send_command("AT^SYSCONFIG=2,6,1,2",  &response);
				}else if ( cm_cu_ct == CMCC ) {
			 		err = at_send_command("AT^SYSCONFIG=2,6,1,2",  &response);
			 	} else {
			 		err = at_send_command("AT^SYSCONFIG=2,6,1,2",  &response);
			 	}
				DO_MM_RESPONSE_ERROR_JDUGE;
				break;
			}
		    case PREF_NET_TYPE_LTE_GSM_WCDMA:
			{
				ALOGI(" PREF_NET_TYPE_LTE_GSM_WCDMA cm_cu_ct = %d",cm_cu_ct);
				if ( cm_cu_ct == CTCC ) {
			 		err = at_send_command("AT^SYSCONFIG=17,6,1,2",  &response);
				}else if ( cm_cu_ct == CUCC ) {
					err = at_send_command("AT^SYSCONFIG=2,6,1,2",  &response);
				}else if ( cm_cu_ct == CMCC ) {
			 		//err = at_send_command("AT^SYSCONFIG=24,7,1,2",  &response);
			 		err = at_send_command("AT^SYSCONFIG=2,6,1,2",  &response);//modified by Boris for 24 bug(TD_GSM_LTE)
				} else {
					err = at_send_command("AT^SYSCONFIG=2,6,1,2",  &response);
				}
				DO_MM_RESPONSE_ERROR_JDUGE;
				break;
			}			
			#endif
#if 0
		   case PREF_NET_TYPE_TD_ONLY:
		   	{
			 	err = at_send_command("AT^SYSCONFIG=15,0,1,2",  &response);
				DO_MM_RESPONSE_ERROR_JDUGE;
				break;
			}
		   case PREF_NET_TYPE_TDDLTE_ONLY:
		   	{
                err = at_send_command("AT^SYSCONFIG=17,0,1,2",  &response);
				DO_MM_RESPONSE_ERROR_JDUGE;
				break;
            }
#endif
			default:
		    {
		        ALOGI("DoNothing");                
		    }
		}
		

		
	RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	goto exit;
	
//#endif

error:
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);


exit:
#ifdef ZX_DT_RAT_HANDOVER	//added by Boris,20160908;Modified by Boris for bug,20161128
	err = at_send_command("AT+CFUN=1",	&response);
	DO_MM_RESPONSE_ERROR_JDUGE;
#endif
	at_response_free(response);
	
}

void ril_request_set_ps_mode(int request, void *data, size_t datalen, RIL_Token token)
{
	  UNUSED(request);
    UNUSED(datalen);
    ATResponse  *response = NULL;
    char    cmdString[64];
    int err = 0, ps_mode = ((int *)data)[0];
    property_set(ZTERILD_PDP_CGDCONT, "0"); 
    
    sprintf(cmdString, "AT+ZSET=\"STANDMODE\",%d", ps_mode);
    err = at_send_command(cmdString, &response);
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

/**
 * RIL_REQUEST_NEIGHBORINGCELL_IDS
 */
void ril_request_get_neighboring_cell_ids(int request, void *data, size_t datalen, RIL_Token token)
{
	UNUSED(request);
	UNUSED(data);
	UNUSED(datalen);
 	int onOff, err, mode, network, i = 0;
	ATResponse	*response = NULL;
	char	*line;
	const struct timeval	TIMEVAL_2s =
	{
		1,
		0
	};
	if (!sScreenState)//if screen state is OFF, CP will not indicate CELL IDS
	{
		goto error;
	}
	sCellNumber = 0;//move forward or it would be late in multi-thread arc.reset cell info counter
	sleep(1);//wait for unsolicited thread to collect cell info,
	RIL_NeighboringCell *result[MAX_NEIGHBORING_CELLS];
	if (sCellNumber > 0 && sCellNumber <= MAX_NEIGHBORING_CELLS)
	{
		for (i = 0; i < sCellNumber; i++)
		{
			result[i] = &sNeighboringCell[i];
		}
		RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sCellNumber * sizeof(RIL_NeighboringCell *));
		for (i = 0; i < sCellNumber; i++)
		{
			FREE(sNeighboringCell[i].cid);
		}
		goto exit;
	}
	else
	{
		goto error;
	}
error:
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
	at_response_free(response);
}

int getrilNetworkType(int mode, int acqorder)
{
    int rilNetworkType = 0;
    if (18 == mode)
    {
        //rilNetworkType = PREF_NET_TYPE_TD_PREF; /* TD prefer */
        rilNetworkType = PREF_NET_TYPE_GSM_WCDMA;
    }
    else if (20 == mode && 7 == acqorder) // 6 == acqorder --> LTE-TD-GSM;7 == acqorder --> LTE-GSM-TD
    {
        //rilNetworkType = PREF_NET_TYPE_TDDLTE_PREF; /* LTE prefer */
        rilNetworkType = PREF_NET_TYPE_LTE_GSM_WCDMA;
    }
    else if (13 == mode)
    {
        rilNetworkType = PREF_NET_TYPE_GSM_ONLY; /* GSM only */
    }
	
#if 0
    else if (15 == mode)
    {
        rilNetworkType = PREF_NET_TYPE_TD_ONLY; /* TD only */
    }
    else if (17 == mode)
    {
        rilNetworkType = PREF_NET_TYPE_TDDLTE_ONLY; /*LTE only */
    }
#endif

    return rilNetworkType;
}

/**
 * RIL_REQUEST_GET_PREFERRED_NETWORK_TYPE
 *
 * Query the preferred network type (CS/PS domain, RAT, and operation mode)
 * for searching and registering.
 */
void ril_request_get_preferred_network_type(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ATResponse  *response = NULL;
    int result[1], mode, acqorder, err;
    char    *line;
	
    err = at_send_command_singleline("AT^SYSCONFIG?", "^SYSCONFIG:", &response);
	DO_MM_RESPONSE_ERROR_JDUGE;
	
    line = response->p_intermediates->line;
    err = at_tok_start(&line);
	DO_ERROR_JDUGE;
	
    err = at_tok_nextint(&line, &mode);
	DO_ERROR_JDUGE;
	
    err = at_tok_nextint(&line, &acqorder);
	DO_ERROR_JDUGE;
	
	result[0] = getrilNetworkType(mode, acqorder);
    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(result));
    goto exit;
error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
    at_response_free(response);
}

//add by ax for TSP 7510 modem begin
void ril_request_set_preferred_plmn_list_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    int         err = 0;
    char cmdString[128];
    ATResponse  *p_response = NULL;
    Preferred_PLMN   *p_args = (Preferred_PLMN *)data;

    /*
        AT+CPOL=[<index>][,<format>[,<oper>[,<GSM_AcT>,<GSM_Compact_AcT>,<UTRAN_AcT>]]]
            If <index> is given but <oper> is left out, entry is deleted.
            If <oper> is given but <index> is left out, <oper> is put in the next free location.
            If only <format> is given, the format of the <oper> in the read command is changed.
    */
    	
    if(p_args&& p_args->index != 0&& (p_args->format == 0 || p_args->format == 1)) {
        /* It is not allowed that add/update UPLMN with <format> = 0/1. */
        ALOGD("It is not allowed that save with <format> = 0/1");
        RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
        return;
    }

    if(p_args->index > 0 && !((p_args->oper) && strlen(p_args->oper) > 0)) {
        /* Delete. */
        snprintf(cmdString, sizeof(cmdString), "AT+CPOL=%d", p_args->index);
    }
    else if(p_args->index <= 0 && ((p_args->oper) && strlen(p_args->oper) > 0)) {
        /* Add without special <index>. */
        snprintf(cmdString, sizeof(cmdString), "AT+CPOL=,%d,\"%s\",%d,%d,%d,%d",
                p_args->format,
                p_args->oper,
                p_args->GSM_AcT,
                p_args->GSM_Compact_AcT,
                p_args->UTRAN_AcT,
                0);
    }
    else if(p_args->index <= 0 && !((p_args->oper) && strlen(p_args->oper) > 0)) {
        /* Set <format> */
        snprintf(cmdString, sizeof(cmdString), "AT+CPOL=,%d", p_args->format);
    }
    else {
        /* Add/Update*/
        snprintf(cmdString, sizeof(cmdString), "AT+CPOL=%d,%d,\"%s\",%d,%d,%d,%d",
                p_args->index,
                p_args->format,
                p_args->oper,
                p_args->GSM_AcT,
                p_args->GSM_Compact_AcT,
                p_args->UTRAN_AcT,
                0);
    }

    err = at_send_command(cmdString, &p_response);
    

    if (err < 0 || p_response->success == 0)
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);

    at_response_free(p_response);

    return;
}

void ril_request_get_preferred_plmn_list_zte(int request, void *data, size_t datalen, RIL_Token token)
	{
		char *line = NULL;
		ATResponse *response = NULL;
		int err = -1, i = 0;
		ATLine *p_cur = NULL;
		Preferred_PLMN** result = NULL;
		int number = 0;
			
		err = at_send_command_multiline ("AT+CPOL?", "+CPOL:", &response);
	
		if (err < 0 || response->success == 0 || !response->p_intermediates)
			goto error;
	
		line = response->p_intermediates->line;
	
		/*
		** multiline response
		*/
		for (p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next) number++;
		result = alloca(number * sizeof(Preferred_PLMN*));
		
		
		i = 0;
		while (i < number)
		{
			result[i] = alloca(sizeof(Preferred_PLMN));
		    i++;
		}
	
		for (i = 0, p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next,i++)
		{
			char *pline = p_cur->line;
			result[i]->index = 0;
			result[i]->format = 2;
			result[i]->oper = NULL;
			result[i]->GSM_AcT = 0;
			result[i]->GSM_Compact_AcT = 0;
			result[i]->UTRAN_AcT = 0;
	
			err = at_tok_start(&pline);
			if (err < 0) goto error;
	
			err = at_tok_nextint(&pline, &(result[i]->index));
			if (err < 0) goto error;
	
			err = at_tok_nextint(&pline, &(result[i]->format));
			if (err < 0) goto error;
	
			err = at_tok_nextstr(&pline, &(result[i]->oper));
			if (err < 0) goto error;
			
			if (!at_tok_hasmore(&pline)) continue;
	
			err = at_tok_nextint(&pline, &(result[i]->GSM_AcT));
			if (err < 0) goto error;
			if (!at_tok_hasmore(&pline)) continue;
	
			err = at_tok_nextint(&pline, &(result[i]->GSM_Compact_AcT));
			if (err < 0) goto error;
			if (!at_tok_hasmore(&pline)) continue;
	
			err = at_tok_nextint(&pline, &(result[i]->UTRAN_AcT));
			if (err < 0) goto error;
		}
	
		RIL_onRequestComplete(token, RIL_E_SUCCESS, result, number * sizeof(Preferred_PLMN*));
		goto exit;
		
error:
		RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
		at_response_free(response);
		
}
//add by ax for TSP 7510 modem end


/**
 * RIL_REQUEST_SET_LOCATION_UPDATES
 *
 * Enables/disables network state change notifications due to changes in
 * LAC and/or CID (basically, *EREG=2 vs. *EREG=1).
 *
 * Note:  The RIL implementation should default to "updates enabled"
 * when the screen is on and "updates disabled" when the screen is off.
 *
 * See also: RIL_REQUEST_SCREEN_STATE, RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED.
 */
void ril_request_set_location_updates(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    ATResponse  *response = NULL;
    char    *cmd;
    //asprintf(&cmd, "AT+CREG=%d", ((int *) data)[0]);
    int err = at_send_command("AT+CREG=2", &response);

    free(cmd);
    if (err < 0 || response->success == 0)
    {
        goto error;
    }

    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
    goto exit;

error:
exit:
    at_response_free(response);
	setCPstandby();
}

static inline int updateValue(int *p, int value)
{
    if (*p != value)
    {
        *p = value;
        return 1;
    }
    return 0;
}

/* Process ind msg of signal length */
void handle_csq(const char *s, const char *smsPdu)
{
    UNUSED(smsPdu);

    char    *line = NULL, *linesave = NULL;
    int err;
    RIL_SignalStrength  result;

    memset(&result, 0, sizeof(result));
    line = strdup(s);
    linesave = line;
    err = at_tok_start(&line);
	DO_ERROR_JDUGE;
    err = at_tok_nextint(&line, &sCSQ[0]);
	DO_ERROR_JDUGE;
    err = at_tok_nextint(&line, &sCSQ[1]);
	DO_ERROR_JDUGE;

    /* CP is asserted or resetting, we need to reset our global variables */
    if (sCSQ[0] == 99 && sCSQ[1] == 99)
    {
        resetLocalRegInfo();
        setRadioState(RADIO_STATE_UNAVAILABLE);

        //disableAllMobileInterfaces();
        result.GW_SignalStrength.signalStrength = 67;
        result.GW_SignalStrength.bitErrorRate = 89;
        RIL_onUnsolicitedResponse(RIL_UNSOL_SIGNAL_STRENGTH, &result, sizeof(result));
    }
    else
    {
        reportSignalStrength(NULL);
    }
	FREE(linesave);
    return;
error:
	FREE(linesave);
    ALOGE("%s: Error parameter in ind msg: %s", __FUNCTION__, s);
    return;
}

void handle_mode(const char *s, const char *smsPdu)
{
    UNUSED(smsPdu);
    char    *line = NULL,
    *linesave = NULL;
    int err;
    int value;
	char mode[5];

    line = strdup(s);
    linesave = line;
    err = at_tok_start(&line);
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextint(&line, &value);
    if (err < 0)
    {
        goto error;
    }

    ALOGD("handle_mode: value=%d, sys_mode=%d",value,sys_mode);
    if(value != sys_mode)
    {
       ALOGD("handle_mode: RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED");
       sys_mode = value;
	   RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED, NULL, 0);
	}
  	sprintf(mode, "%d", value);
	
    ALOGD("handle_mode mode = %s",mode);
    property_set("ril.nwmode", mode);
	
	FREE(linesave);
    return;
error:
    FREE(linesave);
    ALOGE("%s: Error parameter in ind msg: %s", __FUNCTION__, s);
    return;
}

/* Process ind msg of network registration status */
void handle_creg(const char *s, const char *smsPdu)
{
	//ALOGD(" ==== handle_creg bufantong /n");
    UNUSED(smsPdu);
    char    *line = NULL, *linesave = NULL;
	char	char_creg[3];
    int err, changed = 0, value;
    RegState    *p = strStartsWith(s, "+CREG:") ? &sCregState : &sCgregState;
    line = strdup(s);
    linesave = line;
    err = at_tok_start(&line);
	DO_ERROR_JDUGE;
    err = at_tok_nextint(&line, &value);
	DO_ERROR_JDUGE;
    changed |= updateValue(&p->stat, value);
	sprintf(char_creg, "%d", value);
	ALOGD("handle_creg creg = %s",char_creg);
	property_set(ZTERILD_GSM_CREG_STATE,char_creg);
    if (at_tok_hasmore(&line))
    {
        err = at_tok_nexthexint(&line, &value);
		DO_ERROR_JDUGE;
        changed |= updateValue(&p->lac, value);
        if (at_tok_hasmore(&line))
        {
            err = at_tok_nexthexint(&line, &value);
			DO_ERROR_JDUGE;
            changed |= updateValue(&p->cid, value);
            if (at_tok_hasmore(&line))
            {
                err = at_tok_nextint(&line, &value);
				DO_ERROR_JDUGE;
                changed |= updateValue(&sOperInfo.act, value);
            }
        }
    }
    if (changed)
    {
        sOperInfo.mode = -1; // reset local oper info
        sOperInfo.operLongStr[0] = '\0';
        sOperInfo.operShortStr[0] = '\0';
        sOperInfo.operNumStr[0] = '\0';
        RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED, NULL, 0);
    }
    if (linesave != NULL)
    {
        free(linesave);
    }
    return;
error:
    if (linesave != NULL)
    {
        free(linesave);
    }
    ALOGE("%s: Error parameter in ind msg: %s", __FUNCTION__, s);
    return;
}

#if 1
/* Process ind msg of LTE network registration status */ 
void handle_cereg(const char *s, const char *smsPdu)//added by boris for FDD LTE telecom,20160810
{
    UNUSED(smsPdu);
    char    *line = NULL, *linesave = NULL;
	char	char_creg[3];
    int err, changed = 0, value;
    RegState    *p = strStartsWith(s, "+CEREG:") ? &sCregState : &sCgregState;
    line = strdup(s);
    linesave = line;
    err = at_tok_start(&line);
	DO_ERROR_JDUGE;
    err = at_tok_nextint(&line, &value);
	DO_ERROR_JDUGE;
    changed |= updateValue(&p->stat, value);
	sprintf(char_creg, "%d", value);
	ALOGD("handle_cereg cereg = %s",char_creg);
	ALOGD("changed 1 = %d",changed);
	property_set(ZTERILD_GSM_CREG_STATE,char_creg);
    if (at_tok_hasmore(&line))
    {
        err = at_tok_nexthexint(&line, &value);
		DO_ERROR_JDUGE;
        changed |= updateValue(&p->lac, value);
        ALOGD("changed 2 = %d",changed);
        if (at_tok_hasmore(&line))
        {
            err = at_tok_nexthexint(&line, &value);
			DO_ERROR_JDUGE;
            changed |= updateValue(&p->cid, value);
            ALOGD("changed 3 = %d",changed);
            if (at_tok_hasmore(&line))
            {
                err = at_tok_nextint(&line, &value);
				DO_ERROR_JDUGE;
                changed |= updateValue(&sOperInfo.act, value);
                ALOGD("changed 4 = %d",changed);
            }
        }
    }
    if (changed)
    {
        sOperInfo.mode = -1; // reset local oper info
        sOperInfo.operLongStr[0] = '\0';
        sOperInfo.operShortStr[0] = '\0';
        sOperInfo.operNumStr[0] = '\0';
        RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED, NULL, 0);
        ALOGD("RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED is sent");
    }
    if (linesave != NULL)
    {
        free(linesave);
    }
    return;
error:
    if (linesave != NULL)
    {
        free(linesave);
    }
    ALOGE("%s: Error parameter in ind msg: %s", __FUNCTION__, s);
    return;
}
#endif

/*xichunyan 2012-07-27 for change nitz to zmmi  begin */    
/* Process ind msg of network time */
void handle_zmmi(const char *s, const char *smsPdu)
{
    UNUSED(smsPdu);
	char *response, *linesave = NULL, *local_tzone=NULL,*sav_time=NULL;
	int err;
	char *oper_long,*oper_short,*unier_time,*lsa_id, nitztime[25];
	struct timeval	currenttime;
	
    response = linesave = strdup(s);
    err = at_tok_start(&response);
	DO_ERROR_JDUGE;
    err = at_tok_nextstr(&response, &local_tzone);
	DO_ERROR_JDUGE;
    if (at_tok_hasmore(&response))
    {
        err = at_tok_nextstr(&response, &sav_time);
		*sav_time = (sav_time == NULL)?'0':*sav_time;
		DO_ERROR_JDUGE;
        if (!at_tok_hasmore(&response))
        {
            goto exit;
        }
        err = at_tok_nextstr(&response, &oper_long);
		DO_ERROR_JDUGE;
        if (!at_tok_hasmore(&response))
        {
            goto exit;
        }
        err = at_tok_nextstr(&response, &oper_short);
		DO_ERROR_JDUGE;
		gettimeofday(&currenttime, NULL);
        if (at_tok_hasmore(&response))
        {
            err = at_tok_nextstr(&response, &unier_time);                   
			DO_ERROR_JDUGE;
            sprintf(nitztime, "%s%s,%s", unier_time,local_tzone,sav_time);//"yy/mm/dd,hh:mm:ss(+/-)tz,dt" in UTC
			gettimeofday(&lastNitzTimeReceived, NULL);//save last Nitz recevied
            sprintf(lastNitzTime, "%s", response);
            RIL_onUnsolicitedResponse(RIL_UNSOL_NITZ_TIME_RECEIVED, nitztime, strlen(nitztime));
        }
        else if(currenttime.tv_sec - lastNitzTimeReceived.tv_sec < 2)
        {
            sprintf(nitztime, "%s,%s", lastNitzTime, sav_time);//"yy/mm/dd,hh:mm:ss(+/-)tz,dt" in UTC
            RIL_onUnsolicitedResponse(RIL_UNSOL_NITZ_TIME_RECEIVED, nitztime, strlen(nitztime));
        }
    }
	sleep(0.1);
	
	char *cmdString;
	asprintf(&cmdString, "AT+SYCTIME=%u,%u", currenttime.tv_sec,currenttime.tv_usec);
	ALOGE("send AT command: %s",cmdString );
	at_send_command(cmdString, NULL);
	
    goto exit;
error:
    ALOGE("%s: Error parameter in ind msg: %s", __FUNCTION__, s);
exit:	
    FREE(linesave);
    return;
}

void handle_zemci(const char *s, const char *smsPdu)
{
  UNUSED(smsPdu);
  char	*line = NULL;	 
  char	*linesave = NULL;
  int err = -1;
  int totalLen = 0;
  int cmdLen = 0;
  char	*data;

  line = strdup(s);
  linesave = line;
  err = at_tok_start(&line);
  if (err < 0)
  {
	goto error;
  }
  data = strstr(line, " ");
  if (data == NULL)
  {
	goto error;
  }
  cmdLen = strlen(data);
  data[cmdLen] = '\0';
  RIL_onUnsolicitedResponse(RIL_UNSOL_GSM_SERVING_CELL_INFO_ZTE, data, cmdLen);
  /* Free allocated memory and return */
  if (linesave != NULL)
  {
	free(linesave);
  }
  return;

error:
  if (linesave != NULL)
  {
	free(linesave);
  }
  ALOGW("%s: Error parameter in ind msg: %s", __FUNCTION__, s);
  return;
}


void ril_request_set_zemci_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(datalen);
    UNUSED(data);

    int err = 0;
    ATResponse  *response = NULL;

	if (request == RIL_REQUEST_SET_GSM_INFO_PERIODIC_MODE_ZTE)
	{
		err = at_send_command("AT+ZEMCI=1", &response);
	}
	else if(request == RIL_REQUEST_TURN_OFF_GSM_INFO_INDICATOR_ZTE)
	{
		err = at_send_command("AT+ZEMCI=0", &response);
	}

    if (err < 0 || response->success == 0)
	{
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	}
    else
	{
        RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	}
    at_response_free(response);
}

//added by LiaoBin 20161125 for net search

#ifdef CHECK_SYSINFO_ONCE  //gelei  2016-12-09
void check_sysinfo()
{   
 	//while(1)//deleted by boris for AT in thread bug,20161125
#else ifdef CHECK_SYSINFO    //gelei2016-12-09
void *check_sysinfo(void *arg)
{
    while(1) 
#endif		
    {
        if(sys_mode > 0)//report ^mode or no service,do nothing
        {
            ALOGD("check_sysinfo exit");
            return NULL;
        }
        else //sys_mode = 0 or sys_mode = -1
        {
            ATResponse  *response = NULL;
            int err = 0;
            int srv_status = 0;
            int srv_domain = 0;
            int roam_status = 0;
            char    *line;
            err = at_send_command_singleline("AT^SYSINFO", "^SYSINFO:", &response);
        	DO_MM_RESPONSE_ERROR_JDUGE;
            line = response->p_intermediates->line;
            err = at_tok_start(&line);
        	DO_ERROR_JDUGE;
            err = at_tok_nextint(&line, &srv_status);
        	DO_ERROR_JDUGE;
            err = at_tok_nextint(&line, &srv_domain);
        	DO_ERROR_JDUGE;
            err = at_tok_nextint(&line, &roam_status);
        	DO_ERROR_JDUGE;
            err = at_tok_nextint(&line, &sys_mode);
        	DO_ERROR_JDUGE;

            ALOGD("check_sysinfo get sys_mode = %d", sys_mode);
            if(0 == sys_mode)//no service
            {
                usleep(100 * 1000);
            }
            else
            {
                RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED, NULL, 0);
                return NULL;
            }
            error://added by boris for DO_ERROR_JDUGE bug,20161125
    			return err;
        }
    }
    return NULL;
    
}
