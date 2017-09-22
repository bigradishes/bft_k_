/******************************************************************************
 * *(C) Copyright 2012 ZTE International Ltd.
 * * All Rights Reserved
  * *
 * *   EC:		000000000000		
 * *       Reason: indicate stk should stop while sim error & no need zuslot indicate
 * *       Date	20140604 zouxiaojie
 * *   EC:		000000000000		
 * *       Reason: set is icccard exist property
 * *       Date	20140612 zouxiaojie
 * ******************************************************************************/
#include <assert.h>
#include <cutils/properties.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include "at_tok.h"
#include "atchannel.h"
#include "zte-ril.h"
#include "misc.h"
#include "ril-requestdatahandler.h"
#include "work-queue.h"

#define CI_SIM_MAX_CMD_DATA_SIZE  261
#define DO_JDUGE_HAS_MORE \
    do{\
        if (!at_tok_hasmore(&pline))  continue;\
    }while(0)
    
#define ZTERILD_MODEM_CARDMODE_PROPERTY  "persist.radio.isUsimcard"
#define ZTERILD_MODEM_RESETING_PROPERTY  "ril.modemReseting"
#define ZTERILD_MODEM_NEED_PIN_PROPERTY  "ril.modemNeedPin"
#define ZTERILD_STKAPP_READY_PROPERTY  "ril.stkappready"
#define ZTERILD_SIM_STATE					"ril.simstate"

#if 1
//20161223 17.29
char mcc_mnc[][20] = {
	"46000", //0-中国移动
	"46001", //1-中国联通
	"46002", //2-中国移动
	"46003", //3-中国电信
	"46005", //4-中国电信
	"46006", //5-中国联通
	"46007", //6-中国移动
	"46020", //7-中国铁通
	"46011", //8-中国电信
	"46004"  //9-�ڿ� �ƶ�
};

int cm_cu_ct = 0;
#endif

static int  sSimStatus = SIM_NOT_READY;
static RIL_AppType  sAppType = RIL_APPTYPE_UNKNOWN;
int  bNeedDownload = 0;
char *pincode = NULL;
static int pbnum = 0 ;
#define ZTERILD_PBNUM_PROPERTY  "ril.pbnum" 
/*add for STK SET_UP_MENU*/
RIL_CardStatus *getCardStatus(RIL_CardStatus *p_card_status, RIL_CardState card_state, int sim_status, int num_apps);
void getIMSI();

#define SET_AP_READY
#ifdef SET_AP_READY
static int  bFinishStart = 0;
#endif

static RIL_AppStatus  app_status_array[] =
{
  // SIM_ABSENT = 0
  { RIL_APPTYPE_UNKNOWN, RIL_APPSTATE_UNKNOWN, RIL_PERSOSUBSTATE_UNKNOWN,
  NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
  // SIM_NOT_READY = 1
  { RIL_APPTYPE_SIM, RIL_APPSTATE_DETECTED, RIL_PERSOSUBSTATE_UNKNOWN,
  NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
  // SIM_READY = 2
  { RIL_APPTYPE_SIM, RIL_APPSTATE_READY, RIL_PERSOSUBSTATE_READY,
  NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_UNKNOWN },
  // SIM_PIN = 3
  { RIL_APPTYPE_SIM, RIL_APPSTATE_PIN, RIL_PERSOSUBSTATE_UNKNOWN,
  NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN },
  // SIM_PUK = 4
  { RIL_APPTYPE_SIM, RIL_APPSTATE_PUK, RIL_PERSOSUBSTATE_UNKNOWN,
  NULL, NULL, 0, RIL_PINSTATE_ENABLED_BLOCKED, RIL_PINSTATE_UNKNOWN },
  // SIM_NETWORK_PERSONALIZATION = 5
  { RIL_APPTYPE_SIM, RIL_APPSTATE_SUBSCRIPTION_PERSO, RIL_PERSOSUBSTATE_SIM_NETWORK,
  NULL, NULL, 0, RIL_PINSTATE_ENABLED_NOT_VERIFIED, RIL_PINSTATE_UNKNOWN },
  /* SIM_PIN2 = 6 */
  { RIL_APPTYPE_SIM,     RIL_APPSTATE_READY,        RIL_PERSOSUBSTATE_UNKNOWN,
  NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_ENABLED_NOT_VERIFIED },
  /* SIM_PUK2 = 7 */
  { RIL_APPTYPE_SIM,     RIL_APPSTATE_READY,        RIL_PERSOSUBSTATE_UNKNOWN,
  NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_ENABLED_NOT_VERIFIED },
  /* SIM_BLOCK = 8 */
  { RIL_APPTYPE_SIM,     RIL_APPSTATE_READY,        RIL_PERSOSUBSTATE_UNKNOWN,
  NULL, NULL, 0, RIL_PINSTATE_UNKNOWN, RIL_PINSTATE_ENABLED_BLOCKED }
};    
/* Enum value of STK cmd type and STK ind msg type, should be same as in sim_api.h */
/*typedef enum StkCmdTypeTag
{
    STK_CMD_ENABLE_SIMAT = 0,
    STK_CMD_DOWNLOAD_PROFILE = 1,
    STK_CMD_GET_CAP_INFO = 2,
    STK_CMD_GET_PROFILE = 3,
    STK_CMD_SEND_ENVELOPE = 4,

    STK_CMD_PROACTIVE  = 11,
    STK_CMD_SETUP_CALL = 12,
    STK_CMD_DISPLAY_INFO = 13,
    STK_CMD_END_SESSION = 14,
    STK_CMD_SETUP_CALL_STATUS = 15,
    STK_CMD_SETUP_CALL_RESULT = 16,

    STK_CMD_SEND_SM_STATUS = 18,
    STK_CMD_SEND_SM_RESULT = 19,
    STK_CMD_SEND_USSD_RESULT = 20,

    STK_TYPE_INVALID
} StkCmdType;*/
typedef enum
{
  SIM_CARD_TYPE_SIM = 1,
  SIM_CARD_TYPE_USIM = 2,
} SIM_Type;

typedef struct pinRetries_
{
  int pin1Retries; 
  int pin2Retries; 
  int puk1Retries;       
  int puk2Retries;
} pinRetriesStruct;

static int getNumOfRetries(pinRetriesStruct *pinRetries);
 void downloadProfile()
{
  //Ninth byte b7 is LAUNCH BROWSER
  const char  SIM_PROFILE[] = "A010000011FFFFFFFF7F11009F7F0000000000000000";
  const char  USIM_PROFILE[] = "8010000012FFFFFFFF7F1100DFFF000000000000000000";
  char  *cmdString = NULL;

  getAppType();
  const char  *profile = sAppType == RIL_APPTYPE_USIM ? USIM_PROFILE : SIM_PROFILE;
  asprintf(&cmdString, "AT+CSIM=%d,%s", strlen(profile), profile);
  at_send_command_singleline(cmdString, "+CSIM:", NULL);
  free(cmdString);
  getIMSI();
}

char  *Statusarry[] =
{
  "SIM REMOVED",//0
  "",//2
  "READY",//1-1
  "SIM PIN",//3
  "SIM PUK",//4
  "PH-NET PIN",//5
  "SIM PIN2",//6
  "SIM PUK2",//7
  "PH-NETSUB PIN",//8
  "PH-SP PIN",//9
  "PH-CORP PIN",//10
  "PH-SIMLOCK PIN",//11
  "PH-NET PUK",//12
  "PH-NETSUB PUK",//13
  "PH-SP PUK",//14
  "PH-CORP PUK",//15
  "PH-SIMLOCK PUK",//16

};
static int parseSimStatusString(char *line)
{
  int status, err, i;
  char  *result;     

  err = at_tok_start(&line);
  if (err < 0)
  {
    status = SIM_NOT_READY;
    goto done;
  }
  err = at_tok_nextstr(&line, &result);
  if (err < 0)
  {
    status = SIM_NOT_READY;
    goto done;
  }    
  err = 1;
  for (i = 0; i < 17; i++)
  {
    if (0 == strcmp(result, Statusarry[i]))
    {
      status = i;
      err = 0;
      break;
    }
  }
  if (err == 1)
  {
    if (0 != strcmp(result, "READY"))
    {
      /* we're treating unsupported lock types as "sim absent" */
      status = SIM_ABSENT;
      goto done;
    }
  }
done:
  return status;
}

static int parseSimStatus(ATResponse *response)
{
  int status = SIM_NOT_READY;
  char  *cpinLine;
  switch (at_get_cme_error(response))
  {
      case CME_SUCCESS:
      {
        break;
      }
      case CME_SIM_NOT_INSERTED:
      {
        status = SIM_ABSENT;
		//property_set("ril.hasicccard", "0");//unset is icccard exist property zouxiaojie 20140612
		//ALOGD("hasicccard set 0");
        goto done;
      }
      case CME_SIM_UNKNOWN_ERROR:
      {
        if (SIM_ABSENT == sSimStatus)/* some time, CP will return CME_SIM_UNKNOWN_ERROR if there is no SIM card in slot  */
        {
          status = SIM_ABSENT;
          goto done;
        }
        else
        {
          status = SIM_NOT_READY;
          goto done;
        }
      }
      default:
      {
        status = SIM_NOT_READY;
        goto done;
      }
  }
  if (response->p_intermediates == NULL)/* +CPIN? has succeeded, now look at the result */
  {
    status = SIM_NOT_READY;
    goto done;
  }
  cpinLine = response->p_intermediates->line;
  status = parseSimStatusString(cpinLine);
done:
  sSimStatus = status;
  ALOGI("[%s]: set sSimStatus=%d", __FUNCTION__, sSimStatus);
  //set is icccard exist property zouxiaojie 20140612
  if(sSimStatus == SIM_ABSENT)
  {
	property_set("ril.hasicccard", "0");
	ALOGD("hasicccard set 0");
  }
  else if(sSimStatus != SIM_NOT_READY)
  {
	property_set("ril.hasicccard", "1");
	ALOGD("hasicccard set 1");
  }
  return status;
}

/* Do some init work after SIM is ready */
static void onSimInitReady(void *param)
{
  sSimStatus = SIM_READY;
  ALOGI("[%s]: set sSimStatus=%d", __FUNCTION__, sSimStatus);

  int status = getRadioState();
  if ((status != RADIO_STATE_OFF) && (status != RADIO_STATE_UNAVAILABLE) && (status != RADIO_STATE_SIM_READY))
  {
    setRadioState(RADIO_STATE_SIM_READY);
  }
  /* Always send SMS messages directly to the TE, refer to onSmsInitReady() in ril-msg.c */
  at_send_command("AT+CNMI=1,2,2,1,1", NULL);
}


#ifdef SET_AP_READY
static int SetApReady(void)
{
  int pwr_fd = -1;
  char  pwr_buf[32] =
  {
    0
  };
  int pwr_len = 0;
  ssize_t written = 0;

  pwr_fd = open("/sys/ap2cp/ap2cp_power", O_WRONLY);

  ALOGE("Set AP ready open pwr_fd %d \n", pwr_fd);
  if (pwr_fd < 0)
  {
    return -1;
  }

  pwr_len = snprintf(pwr_buf, sizeof(pwr_buf), "%s", "ready");
  written = write(pwr_fd, pwr_buf, pwr_len);
  if (written < 0)
  {
    return -1;
  }
  close(pwr_fd);    

  return 0;
}
#endif

static int isStkAppReady()
{
	char stkAppstate[PROPERTY_VALUE_MAX] = {0};

	if ( 0 == property_get(ZTERILD_STKAPP_READY_PROPERTY, stkAppstate, "0")) 
	{
		ALOGI("isStkAppReady get  switch failed\n");
	} 
	else 
	{
		ALOGI("isStkAppReady get  switch: %s \n",stkAppstate); 
		if (0 == strcmp(stkAppstate, "1"))
		{
			return 1;
		}
		else
		{
			return 0;
		}
	}
	return 0;
}

void getIMSI()
{
    ATResponse  *response = NULL;
    int err;
	char  imsi_str[50] = "";
	int imsi_len = 0;
	char propertieContent[PROPERTY_VALUE_MAX] = {0};
	
	property_get("ril.imsi", propertieContent, "0");
	ALOGD("getIMSI, property_get ril.imsi, propertieContent=%s\n", propertieContent);
	if(0 != strcmp(propertieContent,"0"))
	{	
#if 1
//����Ȳ�� ϵͳ��ȡ��imsi�ᱣ�浽ϵͳ�ļ��
		//\D2ƶ\AF
		if ( 0 == strncmp(imsi_str,mcc_mnc[0],5) || 0 == strncmp(imsi_str,mcc_mnc[2],5) 
			  || 0 == strncmp(imsi_str,mcc_mnc[6],5) || 0 == strncmp(imsi_str,mcc_mnc[9],5) ) {
		  ALOGD("cm_cu_ct CMCC");
		  cm_cu_ct = CMCC;
		} else if ( 0 == strncmp(imsi_str,mcc_mnc[1],5) || 0 == strncmp(imsi_str,mcc_mnc[5],5) ) {
		  ALOGD("cm_cu_ct CUCC");
		  cm_cu_ct = CUCC;
		} else if ( 0 == strncmp(imsi_str,mcc_mnc[3],5) || 0 == strncmp(imsi_str,mcc_mnc[4],5) 
			  || 0 == strncmp(imsi_str,mcc_mnc[8],5) ) {
		  ALOGD("cm_cu_ct CTCC");
		  cm_cu_ct = CTCC;
		} else if ( 0 == strncmp(imsi_str,mcc_mnc[7],5) ) {
		  ALOGD("cm_cu_ct CTT");
		  cm_cu_ct = CTT;
		} else {
		  ALOGD("cm_cu_ct else");
		  cm_cu_ct = 0;
		}
#endif
	  return;
	}
    err = at_send_command_singleline("AT+CIMI", "+CIMI:", &response);
    if (err < 0 || response->success == 0 || response->p_intermediates == NULL)
    {
      goto done;
    }
    else
	{
	  char	*line = NULL;
	  int length = 0;
	  int resp = 0;
	  line = response->p_intermediates->line;	 
	  length = strlen(line);
	  at_tok_start(&line);
	  skipWhiteSpace(&line);
	  imsi_len = strlen(line);
	  strncpy(imsi_str, line, imsi_len);
	  //bufantong 20161223 17.29   2017 01 11 add mcc_mnc[9]
	  #if 1
	  //\D2ƶ\AF
	  if ( 0 == strncmp(imsi_str,mcc_mnc[0],5) || 0 == strncmp(imsi_str,mcc_mnc[2],5) 
	  		|| 0 == strncmp(imsi_str,mcc_mnc[6],5) || 0 == strncmp(imsi_str,mcc_mnc[9],5) ) {
		ALOGD("cm_cu_ct CMCC");
		cm_cu_ct = CMCC;
	  } else if ( 0 == strncmp(imsi_str,mcc_mnc[1],5) || 0 == strncmp(imsi_str,mcc_mnc[5],5) ) {
		ALOGD("cm_cu_ct CUCC");
		cm_cu_ct = CUCC;
	  } else if ( 0 == strncmp(imsi_str,mcc_mnc[3],5) || 0 == strncmp(imsi_str,mcc_mnc[4],5) 
	  		|| 0 == strncmp(imsi_str,mcc_mnc[8],5) ) {
		ALOGD("cm_cu_ct CTCC");
		cm_cu_ct = CTCC;
	  } else if ( 0 == strncmp(imsi_str,mcc_mnc[7],5) ) {
		ALOGD("cm_cu_ct CTT");
		cm_cu_ct = CTT;
	  } else {
		ALOGD("cm_cu_ct else");
		cm_cu_ct = 0;
	  }
	  #endif
	  property_set("ril.imsi", imsi_str);
	  ALOGD("%s,  imsi_str= %s", __FUNCTION__,imsi_str);
	}
    done:
    at_response_free(response);
}


/* External func to query current SIM status */
int getSimStatus(void)
{
  ATResponse  *p_response = NULL;
  int err = -1;
  int status = SIM_NOT_READY;
  char propertieContent[PROPERTY_VALUE_MAX] = {0};
  err = at_send_command_singleline("AT+CPIN?", "+CPIN:", &p_response);
  //getAppType to map responseIccCardStatus, if it's value is UNKNOWN at this time.
  getAppType();
  if (err < 0)
  {
    status = SIM_NOT_READY;
  }
  else
  {
    status = parseSimStatus(p_response);
  }
  if((status == SIM_PIN)||(status == SIM_PUK))
  {
	property_set(ZTERILD_MODEM_NEED_PIN_PROPERTY, "1");
	ALOGD("getSimStatus ZTERILD_MODEM_NEED_PIN_PROPERTY set 1, status=%d", status);
    property_get(ZTERILD_MODEM_NEED_PIN_PROPERTY, propertieContent, "0");
    if(0 != strcmp(propertieContent,"1"))
    {   
	  ALOGD("property_set ZTERILD_MODEM_NEED_PIN_PROPERTY 1 fail, cur=%s\n", propertieContent);
    }
  }
  else
  {
    if(status == SIM_READY)
	{
      getIMSI();
	  property_set(ZTERILD_SIM_STATE, "1"); 
	  ALOGI("set sim state 1");
	}
	property_set(ZTERILD_MODEM_NEED_PIN_PROPERTY, "0");
	ALOGD("getSimStatus ZTERILD_MODEM_NEED_PIN_PROPERTY set 0, status=%d", status);
    property_get(ZTERILD_MODEM_NEED_PIN_PROPERTY, propertieContent, "1");
    if(0 != strcmp(propertieContent,"0"))
    {   
	  ALOGD("property_set ZTERILD_MODEM_NEED_PIN_PROPERTY 0 fail, cur=%s\n", propertieContent);
    }
  }
  at_response_free(p_response);
  if((bNeedDownload ==1)&&(status == SIM_READY)&&(isStkAppReady()==1))
  {
  	bNeedDownload = 0;
	downloadProfile();
  }
#ifdef SET_AP_READY
  ALOGD("getSimStatus status:%d bFinishStart:%d", status, bFinishStart);
  if (bFinishStart == 0)
  {
    if (status == SIM_ABSENT || status == SIM_READY)
    {
      bFinishStart = 1;
      SetApReady();
    }
  }
#endif
  return status;
}

/* External func to query current SIM status */
int correctStandmode(void)
{
  ATResponse  *p_response = NULL;
  int err = -1;
  int status = SIM_NOT_READY;

  err = at_send_command_singleline("AT+CPIN?", "+CPIN:", &p_response);
  if (err < 0)
  {
    status = SIM_NOT_READY;
  }
  else
  {
    status = parseSimStatus(p_response);
  }
  ALOGI("correctStandmode get status = %d\n", status);
  if (status != SIM_NOT_READY)
  {
	getAppType(NULL);
  }
  at_response_free(p_response);
  return status;
}

static int getModemResetingSwitch()
{
	char modemReseting[PROPERTY_VALUE_MAX] = {0};

	if ( 0 == property_get(ZTERILD_MODEM_RESETING_PROPERTY, modemReseting, "0")) 
	{
		ALOGI("getModemResetingSwitch get modem_modemReseting switch failed\n");
	} 
	else 
	{
		ALOGI("getModemResetingSwitch get modem_modemReseting switch: %s \n",modemReseting); 
		if (0 == strcmp(modemReseting, "1"))
		{
			return 1;
		}
		else
		{
			return 0;
		}
	}
	return 0;
}

/* Update radio state according to SIM state */
void updateRadioState(void)
{
  int simStatus = getSimStatus();
  int modemReseting = -1;

  //modemReseting = getModemResetingSwitch();
 // ALOGD(" updateRadioState simStatus=%d, modemReseting=%d", simStatus, modemReseting);
  if ((simStatus == SIM_ABSENT) || (simStatus == SIM_PIN) || (simStatus == SIM_PUK) || (simStatus == SIM_NETWORK_PERSONALIZATION))
  {
    /*if((modemReseting == 1)&&(simStatus == SIM_PIN))//zxj add
    {
      char  *cmdString;	 
      char pincode1[10] = {0};

      if ( 0 == property_get("ril.pincode", pincode1, "FFFF")) 
      {
        ALOGI("3 updateRadioState get pincode  failed :%s\n",pincode1);
      } 
      else 
      {
        ALOGI("3 updateRadioState get pincode : %s \n",pincode1); 
      }
	  if (0 != strcmp(pincode1, "FFFF"))
	  {
	    ALOGD("check pin backgroud in %s, pincode1=%s", __FUNCTION__, pincode1);
	    asprintf(&cmdString, "AT+CPIN=\"%s\"", pincode1);
	    at_send_command_timeout(cmdString, NULL, TIMEOUT_CLCK_CALL_BARRING);
		property_set(ZTERILD_MODEM_NEED_PIN_PROPERTY, "0");
	    ALOGD("updateRadioState ZTERILD_MODEM_NEED_PIN_PROPERTY set 0");
	    free(cmdString);
      }
	  property_set(ZTERILD_MODEM_RESETING_PROPERTY, "0");
    }*/
    setRadioState(RADIO_STATE_SIM_LOCKED_OR_ABSENT);
  }
  else if (simStatus == SIM_READY)
  {
    setRadioState(RADIO_STATE_SIM_READY);
  }
  else if (simStatus == SIM_NOT_READY)
  {
    setRadioState(RADIO_STATE_SIM_NOT_READY);
  }
  else
  {
    ALOGD("Unexpected branch in %s", __FUNCTION__);
  }
}

static int get_test_card_mode()
{
    char propertieContent[PROPERTY_VALUE_MAX] = {0};
    property_get("persist.radio.testcard", propertieContent, "");
	ALOGD("get_test_card_mode propertieContent is: %s", propertieContent);
    if(0 == strcmp(propertieContent,"1"))
    {   
		return 1;
    }
	else
	{
		return 0;
	}
}

static int judge_cmcc_card()
{
    char propertieContent[PROPERTY_VALUE_MAX] = {0};
    property_get("ril.iscmcccard", propertieContent, "");
	ALOGD("judge_cmcc_card propertieContent is: %s", propertieContent);
    if(0 == strcmp(propertieContent,"1"))
    {   
		return 1;
    }
	else
	{
		return 0;
	}
}

static int get_network_mode()
{
    char propertieContent[PROPERTY_VALUE_MAX] = {0};
    property_get("persist.radio.setpsmode", propertieContent, "");
	ALOGD("get_network_mode propertieContent is: %s", propertieContent);
    if(0 == strcmp(propertieContent,"0"))
    {   
		return 0;
    }
	else
	{
		return 1;
	}
}
static int get_standby_mode()
{
   char mode[PROPERTY_VALUE_MAX] = {0};
   property_get("persist.radio.standbymode", mode, "1");
   ALOGD("get_standby_mode mode is: %s", mode);
   if(0 != strcmp(mode,"1"))
   {   
	   return 0; 
   }
   else 
   {
   	   return 1;
   }
}

extern int  reset_zte_ril;
static void hardreset_modem(int mode)
{
    ALOGD("hardreset_modem");
	if(mode == 1)
	{
		at_send_command("AT+ZSET=\"STANDMODE\",1", NULL);
	}
	else
	{
		at_send_command("AT+ZSET=\"STANDMODE\",0", NULL);
	}
	//pollModemNeedPinSwitch();
    setRadioStateOff();
    reset_zte_ril = 1;
    at_send_command("AT", NULL);
}

static void judge_modem_reset(RIL_AppType cardtype)
{
	char iccid[PROPERTY_VALUE_MAX] = {0};
	int testcard = get_test_card_mode();
	int networkmode = get_network_mode();
	int iscmcccard = judge_cmcc_card();

	if(((iscmcccard==1)|| (testcard==1))&&((cardtype == RIL_APPTYPE_USIM)||(cardtype == RIL_APPTYPE_UNKNOWN)))
	{
		property_set("ril.iscmccusim", "1");
		ALOGD(" iscmccusim set 1");
	}
	else
	{
		property_set("ril.iscmccusim", "0");
		ALOGD(" iscmccusim set 0");
	}
	property_get("ril.iccid", iccid, "0");
	property_set("ril.iccid", "0");
    ALOGD("rild ril.iccid=%s", iccid);
	if(0 == strcmp(iccid,"0"))
	{
        return;
	}
	else if(0 == strcmp(iccid,"F"))
	{
	    memset(iccid, 0, sizeof(iccid));
		property_get("ril.iccidbk", iccid, "0");
		ALOGD("rild ril.iccidbk=%s", iccid);
		if((0 == strcmp(iccid,"0"))||(0 == strcmp(iccid,"F")))
		{
	        return;
		}
	}
    ALOGD("rild iccid=%s", iccid);
	if(((iscmcccard==1) || (testcard==1))&&(cardtype == RIL_APPTYPE_USIM)&&(networkmode == 1))
	{
	    if(get_standby_mode() == 0)
	    {
	        ALOGD("is usim and singlemode");
			RIL_onUnsolicitedResponse(RIL_UNSOL_SIM_ICCID, "1", strlen("1"));
	        hardreset_modem(1);
	    }
		else
	    {
	        ALOGD("is usim and dualmode");
			RIL_onUnsolicitedResponse(RIL_UNSOL_SIM_ICCID, "01", strlen("01"));
	    }			
	}
	else 
	{
	    if(get_standby_mode() == 1)
	    {
	        ALOGD("is sim and dualmode");
			RIL_onUnsolicitedResponse(RIL_UNSOL_SIM_ICCID, "0", strlen("0"));
	        hardreset_modem(0);
	    }
		else
		{
	        ALOGD("is sim and singlemode");
			RIL_onUnsolicitedResponse(RIL_UNSOL_SIM_ICCID, "00", strlen("00"));
	    }
	}
}

static void keepAppType(RIL_AppType cardtype)
{
	char propertieContent[PROPERTY_VALUE_MAX] = {0};
	
	ALOGD("keepAppType\n");
	if(cardtype == RIL_APPTYPE_USIM)
	{
		property_set(ZTERILD_MODEM_CARDMODE_PROPERTY, "1");
		property_get(ZTERILD_MODEM_CARDMODE_PROPERTY, propertieContent, "0");
		if(0 != strcmp(propertieContent,"1"))
		{	
			ALOGD("property_set 1 ZTERILD_MODEM_CARDMODE_PROPERTY fail, cur=%s\n",propertieContent);
		}
	}
	else
	{
		property_set(ZTERILD_MODEM_CARDMODE_PROPERTY, "0");
		property_get(ZTERILD_MODEM_CARDMODE_PROPERTY, propertieContent, "1");
		if(0 != strcmp(propertieContent,"0"))
		{	
			ALOGD("property_set 0 ZTERILD_MODEM_CARDMODE_PROPERTY fail, cur=%s\n",propertieContent);
		}
	}
	judge_modem_reset(cardtype);

}

RIL_AppType getKeepAppType()
{
    return sAppType;
}

RIL_AppType getAppType()
{
  if (sAppType == RIL_APPTYPE_UNKNOWN)
  {
    ATResponse  *p_response = NULL;
    int err;
    err = at_send_command_singleline("AT^CARDMODE", "^CARDMODE:", &p_response);
    if (err < 0 || p_response->success == 0 || p_response->p_intermediates == NULL)
    {
      sAppType = RIL_APPTYPE_UNKNOWN;
      goto done;
    }
    else
    {
      char  *line = p_response->p_intermediates->line;
      int type;
      err = at_tok_start(&line);
      if (err < 0)
      {
        sAppType = RIL_APPTYPE_UNKNOWN;
        goto done;
      }
      err = at_tok_nextint(&line, &type);
      if (err < 0)
      {
        sAppType = RIL_APPTYPE_UNKNOWN;
        goto done;
      }
      if (type == SIM_CARD_TYPE_SIM)
      {
        sAppType = RIL_APPTYPE_SIM;
      }
      else if (type == SIM_CARD_TYPE_USIM)
      {
        sAppType = RIL_APPTYPE_USIM;
      }
    }
    done:
    at_response_free(p_response);
	//keepAppType(sAppType);
  }
  ALOGD("%s: UICC type: %d\n", __FUNCTION__, sAppType);
  return sAppType;
}

/**
 * Get the number of retries left for pin functions
 */
static int getNumOfRetries(pinRetriesStruct *pinRetries)
{
  ATResponse  *response = NULL;
  int err = 0;
  char  *line;
  int num_of_retries = -1;
  char  *cmdString;
  
  pinRetries->pin1Retries = -1;
  pinRetries->puk1Retries = -1;
  pinRetries->pin2Retries = -1;
  pinRetries->puk2Retries = -1;
  
  getAppType();
  asprintf(&cmdString, "AT+ZRAP?");
  err = at_send_command_singleline(cmdString, "+ZRAP:", &response);
  if (err < 0 || response->success == 0)
  {
    goto error;
  }
  line = response->p_intermediates->line;
  err = at_tok_start(&line);
  DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &num_of_retries);
  DO_ERROR_JDUGE;
  pinRetries->pin1Retries = num_of_retries;
  err = at_tok_nextint(&line, &num_of_retries);
  DO_ERROR_JDUGE;
  pinRetries->pin2Retries = num_of_retries;
  err = at_tok_nextint(&line, &num_of_retries);
  DO_ERROR_JDUGE;
  pinRetries->puk1Retries = num_of_retries;
  err = at_tok_nextint(&line, &num_of_retries);
  DO_ERROR_JDUGE;
  pinRetries->puk2Retries = num_of_retries;
  error:
  at_response_free(response);
  return err;
}

/* Process AT reply of RIL_REQUEST_ENTER_SIM_XXX */
static void callback_RequestLeftPinRetry(ATResponse *response, RIL_Token token, int request, int is_code_fd)
{
  int err,
  pin1num,
  pin2num,
  puk1num,
  puk2num,
  cardmode = -1;
  char  *line;
  int num_retries[4] =
  {
    0,
    0,
    0,
    0
  };
  if (response->success == 0)
  {
    goto error;
  }
  line = response->p_intermediates->line;
  err = at_tok_start(&line);
    DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &cardmode);
    DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &pin1num);
    DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &pin2num);
    DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &puk1num);
    DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &puk2num);
    DO_ERROR_JDUGE;

  num_retries[0] = pin1num;
  num_retries[1] = pin2num;
  num_retries[2] = puk1num;
  num_retries[3] = puk2num;
  error:
  RIL_onRequestComplete(token, RIL_E_PASSWORD_INCORRECT, &num_retries, sizeof(int) * 4);
}

/**
 * RIL_REQUEST_SIM_IO
 *
 * Request SIM I/O operation.
 * This is similar to the TS 27.007 "restricted SIM" operation
 * where it assumes all of the EF selection will be done by the
 * callee.
 */

#if 1
void ril_request_sim_io(int request, void *data, size_t datalen, RIL_Token token)
{
	char  *cmdString = NULL;
  	ATResponse  *response = NULL;
  	int err, pin2_entered = 0;
  	RIL_Errno ril_errno = RIL_E_GENERIC_FAILURE;
	RIL_SIM_IO_v6 *p_args;
	char *cmd = NULL;
	RIL_SIM_IO_Response sr;
  	char  *line;
	
	memset(&sr, 0, sizeof(sr));
	p_args = (RIL_SIM_IO_v6 *)data;

	if (p_args->pin2) {
		asprintf(cmd, "AT^ZPIN2=\"%s\"", p_args->pin2);
		err = at_send_command(cmd, &response);
		if (err < 0 || NULL == response || response->success == 0) {
			goto error;
		}
		at_response_free(response);
		free(cmd);
		response = NULL;
		cmd = NULL;
	}
	if (p_args->data == NULL) {
		asprintf(&cmdString, "AT+CRSM=%d,%d,%d,%d,%d", p_args->command, p_args->fileid, 
					p_args->p1, p_args->p2, p_args->p3);
	} else {
		asprintf(&cmdString, "AT+CRSM=%d,%d,%d,%d,%d,%s", p_args->command, p_args->fileid, 
					p_args->p1, p_args->p2, p_args->p3, p_args->data);
	}
	err = at_send_command_singleline(cmdString, "+CRSM:", &response);
	if (err < 0 || response->success == 0 || NULL == response ) {
		goto error;
	}

	line = response->p_intermediates->line;
	err = at_tok_start(&line);
  	if (err < 0) goto error;;
  	err = at_tok_nextint(&line, &(sr.sw1));
  	if (err < 0) goto error;;
  	err = at_tok_nextint(&line, &(sr.sw2));
  	if (err < 0) goto error;;
 	 if (at_tok_hasmore(&line))
  	{
  	  err = at_tok_nextstr(&line, &(sr.simResponse));
  	  if (err < 0) goto error;;
  	}
	 RIL_onRequestComplete(token, RIL_E_SUCCESS, &sr, sizeof(sr));
	 at_response_free(response);
    free(cmd);
	return;
error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(response);
    free(cmd);	
}
#endif
#if 0
void ril_request_sim_io(int request, void *data, size_t datalen, RIL_Token token)
{
  UNUSED(request);
  UNUSED(datalen);
//if define REQ_SIM_IO is new  else old
#ifdef REQ_SIM_IO
  RIL_SIM_IO_v6 *p_args = (RIL_SIM_IO_v6 *)data;
#else
  RIL_SIM_IO  *p_args = (RIL_SIM_IO *) data;
#endif

  char  *cmdString = NULL;
  ATResponse  *response = NULL;
  int err, pin2_entered = 0;
  RIL_Errno ril_errno = RIL_E_GENERIC_FAILURE;
  
  if (p_args->pin2)
  {
	char  cmd[32];
	snprintf(cmd, sizeof(cmd), "AT^ZPIN2=\"%s\"", p_args->pin2);
	
#ifdef REQ_SIM_IO
	err = at_send_command(cmd, &response);
#else
	err = at_send_command_timeout(cmd, &response, TIMEOUT_CPIN);
#endif
	
#ifdef REQ_SIM_IO
	if (err < 0 || NULL == response || response->success == 0)
#else
	if (err < 0 || response->success == 0)
#endif
	{
	  at_response_free(response);
	  response = NULL;
#ifdef REQ_SIM_IO
	  
#else
	  ril_errno = RIL_E_SIM_PIN2;
	  RIL_onRequestComplete(token, ril_errno, NULL, 0);
#endif
	  return;
	}
	else
	{
	  at_response_free(response);
		response = NULL;
	}
  }
  
  if (p_args->data == NULL)
  {
    if (p_args->path == NULL)
    {
      asprintf(&cmdString, "AT+CRSM=%d,%d,%d,%d,%d", p_args->command, p_args->fileid, p_args->p1, p_args->p2, p_args->p3);
    }
    else
    {
      asprintf(&cmdString, "AT+CRSM=%d,%d,%d,%d,%d,,\"%s\"", p_args->command, p_args->fileid, p_args->p1, p_args->p2, p_args->p3, p_args->path);
    }
  }
  else
  {
    if (p_args->path == NULL)
    {
      asprintf(&cmdString, "AT+CRSM=%d,%d,%d,%d,%d,%s", p_args->command, p_args->fileid, p_args->p1, p_args->p2, p_args->p3, p_args->data);
    }
    else
    {
      asprintf(&cmdString, "AT+CRSM=%d,%d,%d,%d,%d,%s,\"%s\"", p_args->command, p_args->fileid, p_args->p1, p_args->p2, p_args->p3, p_args->data, p_args->path);
    }
  }   
#ifdef REQ_SIM_IO
  	err = at_send_command_singleline(cmdString, "+CRSM:", &response);
#else 
  	err = at_send_command_singleline_timeout(cmdString, "+CRSM:", &response, TIMEOUT_CRSM);      //gelei   20160928
#endif
  
  if (err < 0 || (response->success == 1 && !response->p_intermediates))
  {
    goto error;
  }
  else if (response->success == 0)
  {
		goto error;
  }
#ifdef REQ_SIM_IO
	else if ( NULL == response )
	{
		goto error;
	}
#endif

  RIL_SIM_IO_Response sr;
  char  *line;
  memset(&sr, 0, sizeof(sr));
  
  line = response->p_intermediates->line;
  
  err = at_tok_start(&line);
  DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &(sr.sw1));
  DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &(sr.sw2));
  DO_ERROR_JDUGE;
  if (at_tok_hasmore(&line))
  {
    err = at_tok_nextstr(&line, &(sr.simResponse));
    DO_ERROR_JDUGE;
  }
  RIL_onRequestComplete(token, RIL_E_SUCCESS, &sr, sizeof(sr));
  goto exit;
error:
  RIL_onRequestComplete(token, ril_errno, NULL, 0);
exit:
  if (cmdString != NULL)
  {
    free(cmdString);
  }
  at_response_free(response);
}
#endif

#if 0
void ril_request_sim_io(int request, void *data, size_t datalen, RIL_Token token)
{
  UNUSED(request);
  UNUSED(datalen);
  RIL_SIM_IO  *p_args = (RIL_SIM_IO *) data;
  char  *cmdString = NULL;
  ATResponse  *response = NULL;
  int err, pin2_entered = 0;
  RIL_Errno ril_errno = RIL_E_GENERIC_FAILURE;

  if (p_args->pin2)
  {
	char  cmd[32];
	snprintf(cmd, sizeof(cmd), "AT^ZPIN2=\"%s\"", p_args->pin2);
	err = at_send_command_timeout(cmd, &response, TIMEOUT_CPIN);
	if (err < 0 || response->success == 0)
	{
	  at_response_free(response);
	  response = NULL;
	  ril_errno = RIL_E_SIM_PIN2;
	  RIL_onRequestComplete(token, ril_errno, NULL, 0);
	  return;
	}
	else
	{
	    at_response_free(response);
		response = NULL;
	}
  }
  
  if (p_args->data == NULL)
  {
    if (p_args->path == NULL)
    {
      asprintf(&cmdString, "AT+CRSM=%d,%d,%d,%d,%d", p_args->command, p_args->fileid, p_args->p1, p_args->p2, p_args->p3);
    }
    else
    {
      asprintf(&cmdString, "AT+CRSM=%d,%d,%d,%d,%d,,\"%s\"", p_args->command, p_args->fileid, p_args->p1, p_args->p2, p_args->p3, p_args->path);
    }
  }
  else
  {
    if (p_args->path == NULL)
    {
      asprintf(&cmdString, "AT+CRSM=%d,%d,%d,%d,%d,%s", p_args->command, p_args->fileid, p_args->p1, p_args->p2, p_args->p3, p_args->data);
    }
    else
    {
      asprintf(&cmdString, "AT+CRSM=%d,%d,%d,%d,%d,%s,\"%s\"", p_args->command, p_args->fileid, p_args->p1, p_args->p2, p_args->p3, p_args->data, p_args->path);
    }
  }   
  //#ifndef LIANTONG 
  	err = at_send_command_singleline_timeout(cmdString, "+CRSM:", &response, TIMEOUT_CRSM);      //gelei   20160928
  //#endif
  if (err < 0 || (response->success == 1 && !response->p_intermediates))
  {
    goto error;
  }
  else if (response->success == 0)
  {
	goto error;
  }
  RIL_SIM_IO_Response sr;
  char  *line;
  memset(&sr, 0, sizeof(sr));
  line = response->p_intermediates->line;
  err = at_tok_start(&line);
  DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &(sr.sw1));
  DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &(sr.sw2));
  DO_ERROR_JDUGE;
  if (at_tok_hasmore(&line))
  {
    err = at_tok_nextstr(&line, &(sr.simResponse));
    DO_ERROR_JDUGE;
  }
  RIL_onRequestComplete(token, RIL_E_SUCCESS, &sr, sizeof(sr));
  goto exit;
error:
  RIL_onRequestComplete(token, ril_errno, NULL, 0);
exit:
  if (cmdString != NULL)
  {
    free(cmdString);
  }
  at_response_free(response);
}
#endif  //bufantong20161207

/**
 * RIL_REQUEST_GET_SIM_STATUS
 *
 * Requests status of the SIM interface and the SIM card.
 *
 * Valid errors:
 *  Must never fail.
 */
void setSetApReady(int sim_status)
{
  if (bFinishStart == 0) /*ALOGD("%s status:%d bFinishStart:%d",__FUNCTION__, sim_status,bFinishStart);*/
  {
    if (sim_status == SIM_ABSENT || sim_status == SIM_READY)
    {
      bFinishStart = 1;
      SetApReady();
    }
  }
}
//added  by liutao 20131203 for TSP 7510 modem and dual standby start	

void  ril_request_get_icc_phonebook_record_info(int request,void *data, size_t datalen, RIL_Token t)
{	
	UNUSED(request);
	UNUSED(data);
	UNUSED(datalen);
	ATResponse *p_response = NULL;
	int err,value = 0;
	int response[12] = {0}, i, j;
	char *line = NULL;
	char *tmp,*tmp2,*tmp3;
   	err = at_send_command_singleline("AT+CPBW=?", "+CPBW:", &p_response);
	if (err < 0 || p_response->success == 0) goto error;
	ALOGD("%s  start!!!",__FUNCTION__);
    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;
    err = at_tok_nextstr(&line, &tmp);
    if (err < 0) goto error;
	err = at_tok_nextint(&line, &response[2]);//number_max_len
	if (err < 0) goto error;
	err = at_tok_nextstr1(&line, &tmp2);
    if (err < 0) goto error;
	err = at_tok_nextstr(&line, &tmp3);
    if (err < 0) goto error;
	err = at_tok_nextint(&line, &response[3]);//name_max_len
	if (err < 0) goto error;
	err = at_tok_nextint(&line, &response[7]);//group_name_max_len
	if (err < 0) goto error;
	err = at_tok_nextint(&line, &response[5]);//second_name_max_len
	if (err < 0) goto error;
	err = at_tok_nextint(&line, &response[11]);//email_max_len
	if (err < 0) goto error; 	
	err = at_send_command("AT+ZCPBQ=?", NULL);
	err = at_send_command("AT+ZCPBQ?", NULL); 
	err = at_send_command("AT+CPBR=?", NULL);	
    response[0] = 1;//first_index
    response[1] = atoi(&tmp[3]);//last_index
	response[4] = 0;//sub_address_max_len
	response[6] = 0;//group_max_num
	response[8] = 1;//ANR  additional_number_max_num
	response[9] = response[2];//additional_number_max_len
	response[10] = 100;//email_max_num
    for (j = 0; j < 12; j++) {
		ALOGD("%s:%d", __FUNCTION__, response[j]);
    }
    RIL_onRequestComplete(t, RIL_E_SUCCESS, response, 12*sizeof(int));
    at_response_free(p_response);
    return;

    error:
    RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(p_response);
	return;

}
static int HandleWriteIccCardRecordNoIndex(void *data)
{
    ATResponse *p_response = NULL;
    char *cmd;
    int err = 0;
    CardRecordStruct *p_args;
    int value= -1;
    char *tmp;
    char *line = NULL;

    p_args = (CardRecordStruct *)data;
    ALOGD("===>  HandleWriteIccCardRecordNoIndex AT^DCPBW");

    asprintf(&cmd,"AT^DCPBW=\"%s\",%d,\"%s\",%d,\"%s\",%d,\"%s\",%d,\"%s\",%d,\"%s\"",
                p_args->num1,
                p_args->type1,
                p_args->num2,
                p_args->type2,
                p_args->num3,
                p_args->type3,
                p_args->num4,
                p_args->type4,
                p_args->name,
                p_args->dcs,
                p_args->email);

    err = at_send_command_singleline(cmd, "^DCPBW:", &p_response);

    if (err < 0 || p_response->success == 0) goto error;

    line = p_response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0) goto error;

    err = at_tok_nextint(&line, &value);
    if (err < 0) goto error;

    if (cmd != NULL)
    {
        free(cmd);
    }

    at_response_free(p_response);
    return value;

    error:

        if (cmd != NULL)
        {
            free(cmd);
        }
        at_response_free(p_response);
        return -1;
}

 void  requestWriteIccCardRecord(int request,void *data, size_t datalen, RIL_Token t)
{
    ATResponse              *p_response = NULL;
    char                    *cmd = NULL,*cmd1 = NULL,*cmd2 = NULL,*cmd3 = NULL,*cmd4 = NULL,*cmd5 = NULL;
    int                             err = 0;
    CardRecordStruct    *p_args = (CardRecordStruct *)data;
	int response[1] = {0};
	if ((p_args->num1 == NULL || strlen(p_args->num1) == 0) &&
         (p_args->num2 == NULL || strlen(p_args->num2) == 0) &&
         (p_args->num3 == NULL || strlen(p_args->num3) == 0) &&
         (p_args->num4 == NULL || strlen(p_args->num4) == 0) &&
         (p_args->name == NULL || strlen(p_args->name) == 0) &&
         (p_args->email == NULL || strlen(p_args->email) == 0))
    {
        asprintf(&cmd,"AT^SCPBW=%d", p_args->index);//delete
        err = at_send_command(cmd, &p_response);
    }
	else
    { 
		asprintf(&cmd,"AT^SCPBW=%d,\"%s\"",p_args->index,p_args->num1);
		ALOGD(" data_cmd:= %s ",cmd);
		if(p_args->type1 == 0){
		  asprintf(&cmd1,",\"%s\"",p_args->num2);
		}else{
		  asprintf(&cmd1,"%d,\"%s\"",p_args->type1,p_args->num2);
		}
		ALOGD(" data_cmd1:= %s ",cmd1);
		if(p_args->type2 == 0){
		  asprintf(&cmd2,",\"%s\"",p_args->num3);
		}else{
		  asprintf(&cmd2,"%d,\"%s\"",p_args->type2,p_args->num3);
		}
		ALOGD(" data_cmd2:= %s ",cmd2);
		if(p_args->type3 == 0){
		  asprintf(&cmd3,",\"%s\"",p_args->num4);
		}else{
		  asprintf(&cmd3,"%d,\"%s\"",p_args->type3,p_args->num4);
		}
		ALOGD(" data_cmd3:= %s ",cmd3);
		if(p_args->type4 == 0){
		  asprintf(&cmd4,",\"%s\",%d,\"%s\"",p_args->name,p_args->dcs,p_args->email);
		}else{
		  asprintf(&cmd4,"%d,\"%s\",%d,\"%s\"",p_args->type4,p_args->name,p_args->dcs,p_args->email);
		}
		ALOGD(" data_cmd4:= %s ",cmd4);
		asprintf(&cmd5,"%s,%s,%s,%s,%s",cmd,cmd1,cmd2,cmd3,cmd4);
		
		ALOGD(" data_cmd5:= %s ",cmd5);
   	err = at_send_command(cmd5, &p_response);
    }
	 
	if (err < 0 || p_response->success == 0)
    {
        if (at_get_cme_error(p_response) == 20)
        {
            response[0] = HandleWriteIccCardRecordNoIndex(data);
            if (response[0] == -1)
            {
                RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
            }
            else
            {
                RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(int));
            }
        }
        else
        {
            RIL_onRequestComplete(t, RIL_E_GENERIC_FAILURE, NULL, 0);
        }
    }
    else
    {
        RIL_onRequestComplete(t, RIL_E_SUCCESS, response, sizeof(int));
    }
	if(cmd != NULL)
	{
		free(cmd);
	}
	if(cmd1 != NULL)
	{
		free(cmd1);
	}
	if(cmd2 != NULL)
	{
		free(cmd2);
	}
	if(cmd3 != NULL)
	{
		free(cmd3);
	}
	if(cmd4 != NULL)
	{
		free(cmd4);
	}
	if(cmd5 != NULL)
	{
		free(cmd5);
    }

    at_response_free(p_response);

    return ;
}

void ril_request_read_icc_card_record_zte(int request, void *data, size_t datalen, RIL_Token token)
{
	UNUSED(request);
	UNUSED(datalen);
	ATLine          *p_cur = NULL;
    int            a = 0;
    int         err = 0;
    int         record_count = 0;
    int         i = 0;
	ATResponse  *p_response = NULL;
    CardRecordStruct   *response = NULL;
    CardRecordStruct   *responses = NULL;
	
	cardrequestStruct  *p_args = (cardrequestStruct *) data;
	char  *cmdString;
	ALOGD("%s start,max num=%d", __FUNCTION__,p_args->max);
	a = p_args->max;
	if(a == pbnum){		
		ALOGD("%s setpbnum!!!", __FUNCTION__);
		property_set(ZTERILD_PBNUM_PROPERTY, "1");
		ALOGD("%s setpbnum end!!!", __FUNCTION__);
		a=0;
	}
	asprintf(&cmdString, "AT^SCPBR=%d,%d", p_args->min, p_args->max);
	
	err = at_send_command_multiline(cmdString, "^SCPBR:", &p_response);
	ALOGD("%s start read  !!!", __FUNCTION__);
	 if (err < 0 || p_response->success == 0 || p_response->p_intermediates==NULL) {
	    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
        return;
    }
	for (p_cur = p_response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next) {
	     record_count++;
    }
    responses = alloca(record_count * sizeof(CardRecordStruct));
    memset(responses, 0x00, (record_count * sizeof(CardRecordStruct)));
    i = 0;
    response = responses;
    for(p_cur=p_response->p_intermediates; p_cur != NULL; p_cur=p_cur->p_next) {
        char    *line = p_cur->line;
        char    *out = NULL;
        /*
        [^SCPBR: <index1>,<num1>,<type>,<num2>,<type>,<num3>,<type>,<num4>,<type>,<text>,<coding>[,<email>][[...]
         ^SCPBR: <index2>,<num1>,<type>,<num2>,<type>,<num3>,<type>,<num4>,<type>,<text>],<coding>[,<email>]]]
        */
        if(i >= record_count) {
            ALOGE("====>requestGetIccCardRecord i(%d) >= record_count(%d)", i, record_count);
            break;
        }
        err = at_tok_start(&line);

        //index
        if(err >= 0)
            err = at_tok_nextint(&line, &(response->index));

        //num1
        {
            err = at_tok_nextstr(&line, &out);
            if(err >= 0) {
                response->num1 = alloca(strlen(out) + 1);
                strcpy(response->num1, out);
		    }
        }

        //type1
        err = at_tok_nextint(&line, &(response->type1));
		
        //num2
        {
            err = at_tok_nextstr(&line, &out);
            if(err >= 0) {
                response->num2 = alloca(strlen(out) + 1);
                strcpy(response->num2, out);
            }
        }

        //type2
        err = at_tok_nextint(&line, &(response->type2));
            
        //num3
        {
            err = at_tok_nextstr(&line, &out);
            if(err >= 0) {
                response->num3 = alloca(strlen(out) + 1);
                strcpy(response->num3, out);
            }
        }

        //type3
        err = at_tok_nextint(&line, &(response->type3));

        //num4
        {
            err = at_tok_nextstr(&line, &out);
            if(err >= 0) {
                response->num4 = alloca(strlen(out) + 1);
                strcpy(response->num4, out);
            }
        }

        //type4
        err = at_tok_nextint(&line, &(response->type4));

        //name
        {
            err = at_tok_nextstr(&line, &out);
            if(err >= 0) {
                response->name = alloca(strlen(out) + 1);
                strcpy(response->name, out);
				
            }
			ALOGD("====> ril_request_read_icc_card_record_zte  type1 ;=%s",response->name);
        }
        //dcs
        err = at_tok_nextint(&line, &(response->dcs));
        //email
        {
            err = at_tok_nextstr(&line, &out);
            if(err >= 0) {
                response->email = alloca(strlen(out) + 1);
                strcpy(response->email, out);
            }
        }
        if (err < 0)
            break;
        response++;
        i++;
    }
    at_response_free(p_response);
    if(responses != NULL)
    {
        RIL_onRequestComplete(token, RIL_E_SUCCESS, responses, (record_count * sizeof(CardRecordStruct)));
    }
    else
    {
       RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    }    
    return;
}
void ril_request_get_record_num_zte(int request, void *data, size_t datalen, RIL_Token token)
{
	UNUSED(request);
	UNUSED(data);
	UNUSED(datalen);
	ATResponse *response = NULL;
    int p_response[2];
    char *line = NULL;
    int err = 0;
    char *tmp;

    err = at_send_command("AT+CPBS=\"SM\"", NULL); 
    err = at_send_command_singleline("AT+CPBS?", "+CPBS:", &response);
    DO_USIM_RESPONSE_JDUGE;
    line = response->p_intermediates->line;
    err = at_tok_start(&line);
    DO_ERROR_JDUGE;

    err = at_tok_nextstr(&line, &tmp);
    DO_ERROR_JDUGE;

    err = at_tok_nextint(&line, &(p_response[0]));
    DO_ERROR_JDUGE;

    err = at_tok_nextint(&line, &(p_response[1]));
    DO_ERROR_JDUGE;
	pbnum = p_response[1];
    ALOGD("====> ril_request_read_icc_card_record_zte  pbnum ;=%d , p_response=%d",pbnum,p_response[1]);
	
    RIL_onRequestComplete(token, RIL_E_SUCCESS, p_response, 2*sizeof(int));
    at_response_free(response);
    return;

    error:
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
        at_response_free(response);
}
//added  by liutao 20131203 for TSP 7510 modem and dual standby end	
void ril_request_get_sim_status(int request, void *data, size_t datalen, RIL_Token token)
{
  UNUSED(request);
  UNUSED(data);
  UNUSED(datalen);
  ATResponse  *response = NULL;
  int err;
  RIL_CardState card_state;
  int num_apps, i, sim_status;
  RIL_CardStatus  *p_card_status;
  pinRetriesStruct  pinRetries;
  err = at_send_command_singleline_timeout("AT+CPIN?", "+CPIN:", &response, TIMEOUT_CPIN);
  DO_ERROR_JDUGE;
  sim_status = parseSimStatus(response);
#ifdef SET_AP_READY       
  setSetApReady(sim_status);
#endif
  if (sim_status == SIM_ABSENT)
  {
    card_state = RIL_CARDSTATE_ABSENT;
    num_apps = 0;
  }
  else
  {
    card_state = RIL_CARDSTATE_PRESENT;
    num_apps = 1;
  }    
  p_card_status = getCardStatus(p_card_status, card_state, sim_status, num_apps);
  RIL_onRequestComplete(token, RIL_E_SUCCESS, (char *) p_card_status, sizeof(RIL_CardStatus));
  if((bNeedDownload ==1)&&(p_card_status->applications[0].app_state== RIL_APPSTATE_READY)&&(isStkAppReady()==1))
  {
  	bNeedDownload = 0;
	downloadProfile();
  }
  free(p_card_status);
  goto exit;
  error:
  RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
  exit:
  at_response_free(response);
}
RIL_CardStatus * getCardStatus(RIL_CardStatus *p_card_status, RIL_CardState card_state, int sim_status, int num_apps)
{
  int i, err;
  pinRetriesStruct  pinRetries;
  p_card_status = malloc(sizeof(RIL_CardStatus));/* Allocate and initialize base card status. */
  p_card_status->card_state = card_state;
  p_card_status->universal_pin_state = RIL_PINSTATE_UNKNOWN;
  p_card_status->gsm_umts_subscription_app_index = -1;
  p_card_status->cdma_subscription_app_index = -1;
  p_card_status->ims_subscription_app_index = -1;
  p_card_status->num_applications = num_apps;    
  for (i = 0; i < RIL_CARD_MAX_APPS; i++)/* Initialize application status. */
  {
    p_card_status->applications[i] = app_status_array[SIM_ABSENT];
  }
  if (sim_status != SIM_ABSENT)
  {
    if (sAppType != RIL_APPTYPE_UNKNOWN)
    {
      for (i = SIM_ABSENT + 1; i < (int)sizeof(app_status_array) / (int)sizeof(app_status_array[0]); ++i)
      {
        app_status_array[i].app_type = sAppType;
      }
    }
  }    
  if (num_apps != 0)/* Pickup the appropriate application status that reflects sim_status for gsm. */
  {
    p_card_status->num_applications = 1;  /* Only support one app, gsm. */
    p_card_status->gsm_umts_subscription_app_index = 0;
    p_card_status->applications[0] = app_status_array[sim_status];/* Get the correct app status. */
    err = getNumOfRetries(&pinRetries);/* Compute the pin state */
    if (err != 0)
    {
      if (pinRetries.puk1Retries == 0)
      {
        p_card_status->applications[0].pin1 = RIL_PINSTATE_ENABLED_PERM_BLOCKED;
      }
      else if (pinRetries.pin1Retries == 0)
      {
        p_card_status->applications[0].pin1 = RIL_PINSTATE_ENABLED_BLOCKED;
      }        
      if (pinRetries.puk2Retries == 0)
      {
        p_card_status->applications[0].pin2 = RIL_PINSTATE_ENABLED_PERM_BLOCKED;
      }
      else if (pinRetries.pin2Retries == 0)
      {
        p_card_status->applications[0].pin2 = RIL_PINSTATE_ENABLED_BLOCKED;
      }
    }
  }
  return (p_card_status);
}
/**
 * Enter SIM PIN, might be PIN, PIN2, PUK, PUK2, etc.
 *
 * Data can hold pointers to one or two strings, depending on what we
 * want to enter. (PUK requires new PIN, etc.).
 *
 */
char * getPinpukCmdbyRequest(int request, void *data)
{
  char  *cmdString;
  char  *code = ((char  **) data)[0];
  if (request == RIL_REQUEST_ENTER_SIM_PIN)
  {
    asprintf(&cmdString, "AT+CPIN=\"%s\"", code);
  }
   //mod by zouxiaojie 2013119 for TSP 7510 modem and dual standby begin
  else if (request == RIL_REQUEST_ENTER_SIM_PIN2)
  {
    asprintf(&cmdString, "AT^ZPIN2=\"%s\"", code);
  }
  else if (request == RIL_REQUEST_GET_PINPUK_RETRIES)
  {
    asprintf(&cmdString, "AT+ZRAP?");
  }
  //mod by zouxiaojie 2013119 for TSP 7510 modem and dual standby end
  else if (request == RIL_REQUEST_ENTER_SIM_PUK || request == RIL_REQUEST_ENTER_SIM_PUK2)
  {
    char  *action = ((char  **) data)[1];
    int fac = (request == RIL_REQUEST_ENTER_SIM_PUK2) ? 1 : 0;
    asprintf(&cmdString, "AT+ZPUK=%d,\"%s\",\"%s\"", fac, code, action);
  }
  else if (request == RIL_REQUEST_CHANGE_SIM_PIN || request == RIL_REQUEST_CHANGE_SIM_PIN2)
  {
    char  *action = ((char  **) data)[1];
    char  *fmt = (request == RIL_REQUEST_CHANGE_SIM_PIN) ? "AT+CPWD=\"SC\",\"%s\",\"%s\"" : "AT+CPWD=\"P2\",\"%s\",\"%s\"";
    asprintf(&cmdString, fmt, code, action);
  }
  else if (request == RIL_REQUEST_SET_FACILITY_LOCK)
  {
    const char  *cb_fac[] ={"AO","OI","OX","AI","IR","AB","AG","AC",NULL};
    char  *action = ((char  **) data)[1];
    char  *password = ((char  **) data)[2];
    int i, clazz = atoi(((char **) data)[3]);
    clazz = (clazz == 0)?255:clazz;
    for (i = 0; cb_fac[i] != NULL; ++i)
    {
      if (!strcmp(code, cb_fac[i]))
      {
        break;
      }
    }
    asprintf(&cmdString, "AT+CLCK=\"%s\",%s,\"%s\",%d", code, action, password, clazz);
  }
  else if (request == RIL_REQUEST_ENTER_NETWORK_DEPERSONALIZATION)
  {
    asprintf(&cmdString, "AT+CLCK=\"PN\",0,\"%s\"", code);
  }
  ALOGD("%s:%s", __FUNCTION__, cmdString);
  return cmdString;
}

void keepPinCode(int request, void *data)
{
    int pinlength = 8;
	char  *code = ((char  **) data)[0];
	int is_code_sc = (strcmp(code, "SC") == 0) ? 1 : 0;    
	//ALOGD("%s:%s:%s:%d", __FUNCTION__, code, action, request);
	if ((request == RIL_REQUEST_ENTER_SIM_PIN)
		||(request == RIL_REQUEST_CHANGE_SIM_PIN)
		||((request == RIL_REQUEST_SET_FACILITY_LOCK)&&(is_code_sc ==1))
		||(request == RIL_REQUEST_ENTER_SIM_PUK))
	{
	    ALOGD("%s:request right set pincode", __FUNCTION__);
		if(pincode == NULL)
		{
		  ALOGD("%s: pincode is NULL.", __FUNCTION__);
		  pincode = (char *)malloc((pinlength +1)*sizeof(char));
		  memset(pincode, 0, (pinlength +1));
		}
	}
	if (request == RIL_REQUEST_ENTER_SIM_PIN)
	{
	  strncpy(pincode, code, pinlength);
	  property_set("ril.pincode", pincode);
	  ALOGD("%s: pincode is %s.", __FUNCTION__, pincode);
	}
	else if((request == RIL_REQUEST_CHANGE_SIM_PIN)||(request == RIL_REQUEST_ENTER_SIM_PUK))
	{
	  char  *action = ((char  **) data)[1];
	  strncpy(pincode, action, pinlength);
	  property_set("ril.pincode", pincode);
	  ALOGD("%s: pincode is %s.", __FUNCTION__, pincode);
	}
	else if((request == RIL_REQUEST_SET_FACILITY_LOCK)&&(is_code_sc ==1))
	{
	  char  *password = ((char  **) data)[2];
	  strncpy(pincode, password, pinlength);
	  property_set("ril.pincode", pincode);
	  ALOGD("%s: pincode is %s.", __FUNCTION__, pincode);
	}
}

static int sendPinpukSuccResponse(int request,RIL_Token token, pinRetriesStruct pinRetries, int is_code_fd)
{
	if(request == RIL_REQUEST_ENTER_SIM_PUK)
	{
	  RIL_onRequestComplete(token, RIL_E_SUCCESS, (void *) &(pinRetries.puk1Retries), sizeof(pinRetries.puk1Retries));
	  getIMSI();
	  property_set(ZTERILD_MODEM_NEED_PIN_PROPERTY, "0");
	  ALOGD("sendPinpukSuccResponse puk ZTERILD_MODEM_NEED_PIN_PROPERTY set 0");
	  property_set(ZTERILD_SIM_STATE, "1"); 
	  ALOGI("set sim state 1");
	  return 0;
	}
	if((request == RIL_REQUEST_ENTER_SIM_PIN2)||(request == RIL_REQUEST_CHANGE_SIM_PIN2))
	{
	  RIL_onRequestComplete(token, RIL_E_SUCCESS, (void *) &(pinRetries.pin2Retries), sizeof(pinRetries.pin2Retries));
	  return 0;
	}
	if(request == RIL_REQUEST_ENTER_SIM_PUK2)
	{
	  RIL_onRequestComplete(token, RIL_E_SUCCESS, (void *) &(pinRetries.puk2Retries), sizeof(pinRetries.puk2Retries));
	  return 0;
	}
	if(request == RIL_REQUEST_SET_FACILITY_LOCK && (is_code_fd))
	{
	  RIL_onRequestComplete(token, RIL_E_SUCCESS, (void *) &(pinRetries.pin2Retries), sizeof(pinRetries.pin2Retries));
	  return 0;
	}
	if(request == RIL_REQUEST_ENTER_SIM_PIN)
	{
	  getIMSI();
	  property_set(ZTERILD_MODEM_NEED_PIN_PROPERTY, "0");
	  ALOGD("sendPinpukSuccResponse pin ZTERILD_MODEM_NEED_PIN_PROPERTY set 0");
	  
	  property_set(ZTERILD_SIM_STATE, "1"); 
	  ALOGI("set sim state 1");
	}
    return 1;
}

static int sendPinpukFailResponse(int request,RIL_Token token, pinRetriesStruct pinRetries, ATResponse  *response, int is_code_fd)
{
	if(request == RIL_REQUEST_ENTER_SIM_PUK)
	{
	    if (response->finalResponse && strStartsWith(response->finalResponse, "+CME ERROR: 16"))
	    {
		  RIL_onRequestComplete(token, RIL_E_PASSWORD_INCORRECT, (void *) &(pinRetries.puk1Retries), sizeof(pinRetries.puk1Retries));
	    }
	    else
	    {
		  RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, (void *) &(pinRetries.puk1Retries), sizeof(pinRetries.puk1Retries));
	    }            
		return 0;
	}
	if((request == RIL_REQUEST_ENTER_SIM_PIN2)||(request == RIL_REQUEST_CHANGE_SIM_PIN2))
	{
		if (response->finalResponse && strStartsWith(response->finalResponse, "+CME ERROR: 16"))
		{
		  RIL_onRequestComplete(token, RIL_E_PASSWORD_INCORRECT, (void *) &(pinRetries.pin2Retries), sizeof(pinRetries.pin2Retries));
		}
		else
		{
		  RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, (void *) &(pinRetries.pin2Retries), sizeof(pinRetries.pin2Retries));
		}			 
		return 0;
	}
	if(request == RIL_REQUEST_ENTER_SIM_PUK2)
	{
		if (response->finalResponse && strStartsWith(response->finalResponse, "+CME ERROR: 16"))
		{
		  RIL_onRequestComplete(token, RIL_E_PASSWORD_INCORRECT, (void *) &(pinRetries.puk2Retries), sizeof(pinRetries.puk2Retries));
		}
		else
		{
		  RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, (void *) &(pinRetries.puk2Retries), sizeof(pinRetries.puk2Retries));
		}			 
		return 0;
	}
	if (request == RIL_REQUEST_SET_FACILITY_LOCK && (is_code_fd))
	{
	  if (response->finalResponse && strStartsWith(response->finalResponse, "+CME ERROR: 16"))
	  {
	    RIL_onRequestComplete(token, RIL_E_PASSWORD_INCORRECT, (void *) &(pinRetries.pin2Retries), sizeof(pinRetries.pin2Retries));
	  }
	  else
	  {
	    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, (void *) &(pinRetries.pin2Retries), sizeof(pinRetries.pin2Retries));
	  }   
	  return 0;
	}
    return 1;
}

void ril_handle_sim_pin_puk(int request, void *data, size_t datalen, RIL_Token token)
{
  UNUSED(datalen); 
  ATResponse  *response = NULL;
  int err;
  char  *cmdString;    
  pinRetriesStruct  pinRetries;
  char  *code = ((char  **) data)[0];
  int is_code_fd = (strcmp(code, "FD") == 0) ? 1 : 0;
  int is_code_sc = (strcmp(code, "SC") == 0) ? 1 : 0;    
  cmdString = getPinpukCmdbyRequest(request, data);
  err = at_send_command_timeout(cmdString, &response, TIMEOUT_CLCK_CALL_BARRING);
  free(cmdString);
  DO_ERROR_JDUGE;
  getNumOfRetries(&pinRetries);
  if (response->success != 0)
  {
    keepPinCode(request, data);
	err = sendPinpukSuccResponse(request, token, pinRetries, is_code_fd);
	if(err == 0)
	{
		goto exit;
	}
    RIL_onRequestComplete(token, RIL_E_SUCCESS, (void *) &pinRetries, sizeof(pinRetries));
    goto exit;
  }
  /* error returned */
  if (request == RIL_REQUEST_SET_FACILITY_LOCK && (!is_code_fd) && (!is_code_sc))    /* not PIN2*//* not PIN */// CLCK return for SS
  {
    if (response->finalResponse && strStartsWith(response->finalResponse, "+CME ERROR: 16"))
    {
      RIL_onRequestComplete(token, RIL_E_PASSWORD_INCORRECT, (void *) &pinRetries, sizeof(pinRetries));
    }
    else
    {
      RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, (void *) &pinRetries, sizeof(pinRetries));
    }            
    goto exit;
  }
  err = sendPinpukFailResponse(request, token, pinRetries,response, is_code_fd);
  if(err == 0)
  {
	  goto exit;
  }
  RIL_onRequestComplete(token, RIL_E_PASSWORD_INCORRECT, &pinRetries, sizeof(pinRetries));
  goto exit;
  error:
  RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
  exit:
  at_response_free(response);
}

//added  by zxj 20131209 for TSP 7510 modem and dual standby end
/**
 * RIL_REQUEST_GET_IMSI
 */
void ril_request_get_imsi(int request, void *data, size_t datalen, RIL_Token token)
{
  UNUSED(request);
  UNUSED(data);
  UNUSED(datalen);
  ATResponse  *response = NULL;
  int err;
  char  *cmdString;    
  char  imsi_str[50] = "";
  int imsi_len = 0;
  asprintf(&cmdString, "AT+CIMI");
  err = at_send_command_singleline(cmdString, "+CIMI:", &response);
  free(cmdString);
  if (err < 0 || response->success == 0 || response->p_intermediates == NULL)
  {
    goto error;
  }
  {
    char  *line = NULL;
    int length = 0;
    int resp = 0;
    line = response->p_intermediates->line;    
    length = strlen(line);
    at_tok_start(&line);
    skipWhiteSpace(&line);
    imsi_len = strlen(line);
    strncpy(imsi_str, line, imsi_len);
    strncpy(response->p_intermediates->line, imsi_str, length);
	property_set("ril.imsi", imsi_str);
	strncpy(response->p_intermediates->line, imsi_str, length);
	ALOGD("%s,	imsi_str= %s", __FUNCTION__,imsi_str);
  }
  if (response->finalResponse)
  {
    RIL_onRequestComplete(token, RIL_E_SUCCESS, response->p_intermediates->line, sizeof(char *));
    goto exit;
  }
  error:
  RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
  exit:
  at_response_free(response);
}


char * getStkCmdbyRequest(int request)
{
  char  *data_m = NULL;
  if (request == RIL_REQUEST_STK_SEND_ENVELOPE_COMMAND)
  {
    data_m = (sAppType == RIL_APPTYPE_USIM) ? "80C20000" : "A0C20000";
  }
  else if (request == RIL_REQUEST_STK_SET_PROFILE)
  {
    data_m = (sAppType == RIL_APPTYPE_USIM) ? "80100000" : "A0100000";
  }
  else if (request == RIL_REQUEST_STK_SEND_TERMINAL_RESPONSE)
  {
    data_m = (sAppType == RIL_APPTYPE_USIM) ? "80140000" : "A0140000";
  }
  return data_m;
}

/**
 * RIL_REQUEST_STK_GET_PROFILE
 * RIL_REQUEST_STK_SEND_ENVELOPE_COMMAND
 *
 * Requests the profile of SIM tool kit.
 * The profile indicates the SAT/USAT features supported by ME.
 * The SAT/USAT features refer to 3GPP TS 11.14 and 3GPP TS 31.111.
 */
void ril_handle_stk_cmd_tsp(int request, void *data, size_t datalen, RIL_Token token)
{
  UNUSED(datalen);
  ATResponse  *response = NULL;
  int err = 0;
  char  *cmdString;    
  char  datalentmp[3] = {0};
  char  *data_m = NULL;
  int nlen = strlen(data) / 2;
  if (nlen <= 0xFF)
  {
    if (nlen <= 0x0F)
    {
      sprintf(datalentmp, "0%X", nlen);
    }
    else
    {
      sprintf(datalentmp, "%X", nlen);
    }
  }
  data_m = getStkCmdbyRequest(request);    
  if (request == RIL_REQUEST_STK_GET_PROFILE || request == RIL_REQUEST_STK_HANDLE_CALL_SETUP_REQUESTED_FROM_SIM)
  {
    goto error;
  }
  asprintf(&cmdString, "AT+CSIM=%d,%s%s%s", strlen(data_m) + strlen(datalentmp) + strlen(data), data_m, (char *) datalentmp, (char *) data);
  err = at_send_command_singleline_timeout(cmdString, "+CSIM:", &response, TIMEOUT_CRSM);
  free(cmdString);
  DO_RESPONSE_ERROR_JDUGE;
  char  *line = NULL;
  int length, resp = 0;
  line = response->p_intermediates->line;
  at_tok_start(&line);
  err = at_tok_nexthexint(&line, &length);
  DO_ERROR_JDUGE;
  err = at_tok_nexthexint(&line, &resp);
  DO_ERROR_JDUGE;
  if ((4 == length) && ((0x9000 == resp)||(0x6F00 == resp)))
  {
    RIL_onUnsolicitedResponse(RIL_UNSOL_STK_SESSION_END, NULL, 0);
  }
  if (response->finalResponse)
  {
    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
    goto exit;
  }
error:
  RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
  //20140604 zouxiaojie add for sim error protect
  RIL_onUnsolicitedResponse(RIL_UNSOL_STK_SESSION_END, NULL, 0);
  ALOGD("error RIL_UNSOL_STK_SESSION_END send");
exit:
  at_response_free(response);
}

#ifdef ZTE_RIL_EXTEND
void ril_request_do_sim_auth_zte(int request, void *data, size_t datalen, RIL_Token token)
{
  char  *line = NULL;
  char  cmdString[MAX_AT_LENGTH];
  ATResponse  *response = NULL;
  int err = -1;
  int iStatus = -1;
  RIL_SIM_AUTH_Cnf  s_SimAuth =
  {
    NULL,
    NULL
  };
  char  *rand = (char *) data;
  snprintf(cmdString, sizeof(cmdString), "AT^MBAU=\"%s\"", rand);
  err = at_send_command_singleline(cmdString, "^MBAU:", &response);
  DO_RESPONSE_JDUGE;
  line = response->p_intermediates->line;
  err = at_tok_start(&line);
  DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &iStatus);
  DO_ERROR_JDUGE;
  switch (iStatus)
  {
      case 0:
      //Auth success, return <kc>,<sres>
      {
        err = at_tok_nextstr(&line, &(s_SimAuth.kc));
            DO_ERROR_JDUGE;
        err = at_tok_nextstr(&line, &(s_SimAuth.sres));
            DO_ERROR_JDUGE;
        break;
      }
      case 2:
      //Authentication error, incorrect MAC
      {
        break;
      }
      case 3:
      //Unsupported security context
      {
        break;
      }
      default:
      goto error;
      break;
  }
  RIL_onRequestComplete(token, RIL_E_SUCCESS, &s_SimAuth, sizeof(s_SimAuth));
  goto exit;
  error:
  RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
  exit:
  at_response_free(response);
}

void ril_request_do_usim_auth_zte(int request, void *data, size_t datalen, RIL_Token token)
{
  char  *line = NULL;
  char  cmdString[MAX_AT_LENGTH];
  ATResponse  *response = NULL;
  int err, iStatus = -1;
  RIL_USIM_AUTH_Cnf s_UsimAuth = {-1,NULL,NULL,NULL,NULL};
  char  *rand = ((char  **) data)[0];
  char  *autn = ((char  **) data)[1];
 
  snprintf(cmdString, sizeof(cmdString), "AT^MBAU=\"%s\",\"%s\"", rand, autn);
  err = at_send_command_singleline(cmdString, "^MBAU:", &response);
  DO_RESPONSE_ERROR_JDUGE;
  line = response->p_intermediates->line;
  err = at_tok_start(&line);/*success ^MBAU:<status>,<Ck>,<Ik>,<Res> fail ^MBAU:<status>,,<Auts>*/
  DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &iStatus);
  DO_ERROR_JDUGE;
  s_UsimAuth.status = iStatus;  // status
  switch (iStatus)
  {
      case 0:// Auth success, return <Ck>,<Ik>,<Res>
      {
        err = at_tok_nextstr(&line, &(s_UsimAuth.ck));  // <res>
        DO_ERROR_JDUGE;
        err = at_tok_nextstr(&line, &(s_UsimAuth.ik));
        DO_ERROR_JDUGE;
        err = at_tok_nextstr(&line, &(s_UsimAuth.res_auts));
        DO_ERROR_JDUGE;            
        break;
      }
      case 1:// Synchronisation failure, return auts
      {
        err = at_tok_nextstr(&line, &(s_UsimAuth.res_auts));
        DO_ERROR_JDUGE;
        break;
      }
      case 2:// Authentication error, incorrect MAC
      case 3:// Unsupported security context
      {
        break;            // nothing to do
      }        
      default:
      {
        goto error;//break;
      }
  }
  RIL_onRequestComplete(token, RIL_E_SUCCESS, &s_UsimAuth, sizeof(s_UsimAuth));
  goto exit;
  error:
  RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
  exit:
  at_response_free(response);
}
//mod by zouxiaojie 2013119 for TSP 7510 modem and dual standby begin
void ril_request_get_pinpuk_retries_zte(int request, void *data, size_t datalen, RIL_Token token)
{
  ATResponse  *response = NULL;
  int err;
  char  *cmdString;
  char  *line = NULL;
  int num_retries[4] ={0,0,0,0};

  getAppType();
  asprintf(&cmdString, "AT+ZRAP?");
  err = at_send_command_singleline(cmdString, "+ZRAP:", &response);
  DO_MM_RESPONSE_ERROR_JDUGE;
  if (strStartsWith(response->finalResponse, "+CME ERROR:") || response->p_intermediates == NULL)
  {
    goto error;
  }
  line = response->p_intermediates->line;
  err = at_tok_start(&line);
  DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &num_retries[0]);
  DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &num_retries[1]);
  DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &num_retries[2]);
  DO_ERROR_JDUGE;
  err = at_tok_nextint(&line, &num_retries[3]);
  DO_ERROR_JDUGE;
  RIL_onRequestComplete(token, RIL_E_SUCCESS, &num_retries, sizeof(int) * 4);
  goto exit;
error:
  RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
  exit:
  at_response_free(response);
}
//mod by zouxiaojie 2013119 for TSP 7510 modem and dual standby end
#endif /* ZTE_RIL_EXTEND */

void ril_request_report_stk_service_is_running(int request, void *data, size_t datalen, RIL_Token token)
{
  UNUSED(request);
  UNUSED(data);
  UNUSED(datalen);
  //property_set(ZTERILD_STKAPP_READY_PROPERTY, "1");
  ALOGI("zxj ril_request_report_stk_service_is_running ZTERILD_STKAPP_READY_PROPERTY bNeedDownload=%d\n", bNeedDownload);
  if(bNeedDownload ==1)
  {
	  downloadProfile();
	  bNeedDownload =0;
  }
  RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
}

void ril_request_stk_send_envelope_with_status(int request, void *data, size_t datalen, RIL_Token token)
{
  UNUSED(data);
  UNUSED(datalen);    
  ATResponse  *response = NULL;
  int err = 0; 
  char  *cmdString;    
  char  datalentmp[3] = {0};
  char  *data_m = NULL;            
  int nlen = strlen(data) / 2;
  sprintf(datalentmp, "%X", nlen - 1);
  data_m = "80C20000";
  asprintf(&cmdString, "AT+CSIM=%d,%s%s%s", strlen(data_m) + strlen(datalentmp) + strlen(data), data_m, (char *) datalentmp, (char *) data);
  err = at_send_command_singleline_timeout(cmdString, "+CSIM:", &response, TIMEOUT_CRSM);
  free(cmdString);
  DO_RESPONSE_ERROR_JDUGE;
  {
    //ALOGW("%s:StkResponse:%s\n", __FUNCTION__, response->p_intermediates->line);
    char  *line = NULL;
    int length, resp = 0;
    line = response->p_intermediates->line;
    at_tok_start(&line);
    err = at_tok_nexthexint(&line, &length);
    DO_ERROR_JDUGE;
    err = at_tok_nexthexint(&line, &resp);
    DO_ERROR_JDUGE;
    if ((4 == length) && ((0x9000 == resp)||(0x6F00 == resp)))
    {
      ALOGE("%s: callback_StkResponse RIL_UNSOL_STK_SESSION_END", __FUNCTION__);
      RIL_onUnsolicitedResponse(RIL_UNSOL_STK_SESSION_END, NULL, 0);
    }
  }
  if (response->finalResponse)
  {
    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
    goto exit;
  }    
error:
  RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
  at_response_free(response);
}

/* +ZUFCH:len,data 9000*/
void handle_zufch(const char *s, const char *smsPdu)
{
  UNUSED(smsPdu);

  char  *line = NULL;    
  char  *linesave = NULL;
  int err = -1;
  int totalLen = 0;
  int cmdLen = 0;
  char  *data;

  line = strdup(s);
  linesave = line;
  err = at_tok_start(&line);
  if (err < 0)
  {
    goto error;
  }
  err = at_tok_nextint(&line, &totalLen);
  if (err < 0)
  {
    goto error;
  }
  data = strstr(line, "D0");
  if (data == NULL)
  {
    goto error;
  }
  else // ZUFCH:len, XXXX9000
  {
    cmdLen = strlen(data) - 4;
    data[cmdLen] = '\0';
    ALOGD("handle_stk cmdLen=%d\n", cmdLen);
  }
  RIL_onUnsolicitedResponse(RIL_UNSOL_STK_PROACTIVE_COMMAND, data, cmdLen);
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

void parseAppTypeTsp(int appMode)
{
  if (appMode == 1)
  {
    sAppType = RIL_APPTYPE_USIM;
  }
  else if (appMode == 2)
  {
    sAppType = RIL_APPTYPE_SIM;
  }
  else
  {
    sAppType = RIL_APPTYPE_UNKNOWN;
  }
}

/* SIM init result */
int parseUsimInitResult(char *line)
{
  int err = -1;
  int appMode = -1;
  int initResult = -1;
  int status = SIM_NOT_READY;

  err = at_tok_start(&line);
  if (err < 0)
  {
    goto error;
  }
  err = at_tok_nextint(&line, &appMode);
  if (err < 0)
  {
    goto error;
  }
  err = at_tok_nextint(&line, &initResult);
  if (err < 0)
  {
    goto error;
  }

  ALOGD("%s: appMode: %d, initResult: %d\n", __FUNCTION__, appMode, initResult);

  switch (initResult)
  {
      case 10:
      status = SIM_ABSENT;
      break;
      case 13:
      status = SIM_NOT_READY;
      break;
      case 15:
      status = SIM_NOT_READY;
      parseAppTypeTsp(appMode);
      break;
      case 30:
      case 31:
      status = SIM_READY;
      parseAppTypeTsp(appMode);
      break;
      default:
      status = SIM_NOT_READY;
      break;
  }

  return status;
error:
  return SIM_NOT_READY;
}

/* +ZURDY:<appmode>,<initresult> */
void handle_zurdy(const char *s, const char *smsPdu)
{
  UNUSED(smsPdu);

  char  *linesave = NULL;
  int oldStatus = sSimStatus;
  int simStatus = SIM_NOT_READY;

  linesave = strdup(s);
  simStatus = parseUsimInitResult(linesave);
  ALOGD("handle_zurdy simStatus= %d\n", simStatus);
  if (simStatus != oldStatus)
  {
    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED, NULL, 0);
  }

  /* we must judge SIM_READY due to SIM Status can't be control by Radio*/
  if (simStatus == SIM_READY)
  {
    /* when SIM Ready indication coming, we need to wait EUICC report SIM type
     * onSimInitReady will do STK init, and according to the Radio state to change
     * Radio state, so it must be check firstly */
    const struct timeval  TIMEVAL_DELAY = {1, 0};
    enqueDelayed(getWorkQueue(SERVICE_SIM), onSimInitReady, NULL, &TIMEVAL_DELAY);
  }
  else if (getRadioState() == RADIO_STATE_OFF)
  {
    ;//do nothing
  }
  else if (simStatus == SIM_NOT_READY)
  {
    setRadioState(RADIO_STATE_SIM_NOT_READY);
  }
  else if ((simStatus == SIM_PIN2) || (simStatus == SIM_PUK2))
  {
    ; // do nothing
  }
  else
  {
    setRadioState(RADIO_STATE_SIM_LOCKED_OR_ABSENT);
  }

  /* Free allocated memory and return */
  if (linesave != NULL)
  {
    free(linesave);
  }
  return;
}
void handle_zuslot(const char *s, const char *smsPdu)
{
	UNUSED(smsPdu);
	char  *line = NULL;    
	char  *linesave = NULL;
	int err = -1;
	int slot = 0;
	line = strdup(s);
	linesave = line;
	
	err = at_tok_start(&line);
	if (err < 0)
	{
	  goto error;
	}
	err = at_tok_nextint(&line, &slot);
	if (err < 0)
	{
	  goto error;
	}
	if(slot == 0)//one card 
	{
	//20140604 zouxiaojie del for no need zuslot indicate
		//RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED, NULL, 0);
		//property_set("ril.hasicccard", "0");
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
	return;
}

/**
 * RIL_REQUEST_LTE_AUTH
 */
void ril_request_lte_auth(int request, void *data, size_t datalen, RIL_Token token)
{

	ATResponse *response = NULL;
	int err;
    char cmdString[MAX_AT_LENGTH] = { 0 };
	char imsi_str[50] = "";
	int randnum = 0;
	int auth = 0;
	
    sprintf(cmdString, "AT^MBAU=%d,%d", randnum, auth);
	err = at_send_command_singleline(cmdString, "^MBAU", &response);
	//free(cmdString);
	DO_ERROR_JDUGE;

	if (response->finalResponse)
	{
		RIL_onRequestComplete(token, RIL_E_SUCCESS, response->p_intermediates->line, sizeof(char *));
		goto exit;
	}
error:
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
	at_response_free(response);
}

#if 0
static char stkSetUpMenuRawData[CI_SIM_MAX_CMD_DATA_SIZE * 2 + 1] = "\0";

/* +ZUINIT:<uiccstate>,<appmode>,<iccid>[,<phase>][,<Li>][,<Pl>] */
static int parseUsimStatusTsp(ATResponse*  response)
{
    int       err,uiccState,appMode= -1;
    int    status = SIM_NOT_READY;
    char  *iccID= NULL;
    char* p_cur = NULL;
    DO_USIM_RESPONSE_JDUGE;
    p_cur = response->p_intermediates->line;
    err = at_tok_start(&p_cur);
    DO_ERROR_JDUGE;    
    err = at_tok_nextint(&p_cur, &uiccState);
    DO_ERROR_JDUGE;
    err = at_tok_nextint(&p_cur, &appMode);
    DO_ERROR_JDUGE;
    switch(uiccState)
    {
        case 10:
            status = SIM_ABSENT;
            break;
        case 11:
            status = SIM_PIN;
            parseAppTypeTsp(appMode);
            break;
        case 12:
            status = SIM_PUK;
            parseAppTypeTsp(appMode);
            break;
        case 13:
        case 15:                
        case 30:
        case 260:
        case 261:    
            status = SIM_NOT_READY;
            parseAppTypeTsp(appMode);
            break;
        default: 
            status = SIM_NOT_READY;
            break;
    }//    sSimStatus = status;
    return status;
error:
    status = SIM_NOT_READY;//sSimStatus = status;
    return status;
}

/*
static int activeUsimCard(void)
{
    ATResponse *p_response = NULL;
    int err, status;
//ֱ\BDӶԿ\A8\B2۸\B3ֵ0
    err = at_send_command_singleline("AT+ZUINIT=0,1,2,0", "+ZUINIT:",  &p_response);
    if (err < 0)
    {
        status = SIM_NOT_READY;
    }
    
    else
    {
        status = parseUsimStatusTsp(p_response);
    }

    at_response_free(p_response);
    return status;
}


void activeUsimforTsp(void)
{
    int simStatus = SIM_NOT_READY;

    simStatus = activeUsimCard();

    ALOGD("activeUsimforTsp simStatus=%d\n", simStatus);

    if ((simStatus == SIM_ABSENT) || (simStatus == SIM_PIN)
        || (simStatus == SIM_PUK)  || (simStatus == SIM_NETWORK_PERSONALIZATION))
    {
        setRadioState(RADIO_STATE_SIM_LOCKED_OR_ABSENT);
    }
    else if (simStatus == SIM_READY)
    {
        setRadioState(RADIO_STATE_SIM_READY);
    }
    else if (simStatus == SIM_NOT_READY)
    {
        ALOGD("%s ZUINIT return SIM_NOT_READY", __FUNCTION__);
        //setRadioState(RADIO_STATE_SIM_NOT_READY);
    }
    else
    {
        ALOGD("Unexpected branch in %s", __FUNCTION__);
    }
}

static void requestSetUpMenu() {
    if (stkSetUpMenuRawData[0] != '\0') {
        stkSetUpMenuFlag = 1;
        RIL_onUnsolicitedResponse(RIL_UNSOL_STK_PROACTIVE_COMMAND,
                &stkSetUpMenuRawData, strlen(stkSetUpMenuRawData));
    }
}

static void libSaveSetUpMenu(char * data, int len) {
    char temp1, temp2;
    char *c = data;

    if ((*c++ == 'D') && (*c++ == '0') && len <= CI_SIM_MAX_CMD_DATA_SIZE * 2) {
        temp1 = *c++;
        temp2 = *c++;
        if ((int) (temp1 & 0xff) < 0x38) {
            if (c[0] == '0' && c[1] == '1' && c[2] == '0' && c[3] == '3'
                    && c[6] == '2' && c[7] == '5') {
                memcpy(&stkSetUpMenuRawData, data, len);
                ALOGD("%s: command Detail is STK SET_UP_MENU, save SET_UP_MENU \n", __FUNCTION__);
            }
        } else if (temp1 == '8' && temp2 == '1') {
            if (c[2] == '0' && c[3] == '1' && c[4] == '0' && c[5] == '3'
                    && c[8] == '2' && c[9] == '5') {
                memcpy(&stkSetUpMenuRawData, data, len);
                ALOGD("%s: command Detail is STK SET_UP_MENU, save SET_UP_MENU \n", __FUNCTION__);
            }
        } else {
            ALOGE("%s: the length of Indication is not correct\n", __FUNCTION__);
        }
    } else {
        ALOGD("%s: This is not proactive command \n", __FUNCTION__);
    }
}*/

/* Sometimes when receiving +CPIN: READY, but network is not registered, to speed up registration,
 * RADIO_STATE_SIM_READY will be sent later after network is registered
 */
void notifyRegisteredToSim(void)
{
  int status = getRadioState();

  if ((sSimStatus == SIM_READY) && (status != RADIO_STATE_SIM_READY))
  {
    setRadioState(RADIO_STATE_SIM_READY);
  }
}

#endif
