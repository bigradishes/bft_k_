/******************************************************************************
 * *(C) Copyright 2012 ZTE International Ltd.
 * * All Rights Reserved
 * *
 * * 1.    EC: 000000000000    Reason: there may already be active pdp context for 3g mode(4g switch to 3g),    Date: 20140604
 * *                                       if we are defining new pdp context, deactivate it first;
 * * 2.    EC: 617002786949    Reason: updated by zhangzhiliang, 20140609, for apn type "default" or "*",          Date: 20140609
 * *                                       allocate cid 1, otherwise, non "cmnet" apn get cid 2 allocated after ps
 * *                                       transfer, as cid 1 is active after syncPdpContext
 * *
 * ******************************************************************************/

#include <arpa/inet.h>
#include <cutils/properties.h>
#include "at_tok.h"
#include "atchannel.h"
#include "zte-ril.h"
#include "misc.h"
#include "ril-requestdatahandler.h"
#include "work-queue.h"
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>

#undef LOG_TAG
#define LOG_TAG "RILC-PS"

#define LOG_DEBUG 0

#undef __LOGD
#if LOG_DEBUG
#define __LOGD(format,...) RLOGD(format"\t\t %s() <"__FILE__":%d>",##__VA_ARGS__,__FUNCTION__,__LINE__)
#else
#define __LOGD(format,...) RLOGD(format"\t\t <%s():%d>",##__VA_ARGS__,__FUNCTION__,__LINE__)
//#define __LOGD(format,...)   ((void)0)
#endif

//#define DEV_NETCARD "eth"
#define DEV_NETCARD "lte" //chenjun 20170503

extern int getCurNwMode();
extern int is_ppp_enabled();
extern void enableInterface(int cid);
extern void disableEthInterface(int cid);
extern int configureInterface(char *ifname,char* address);     
extern void deconfigureInterface(char *ifname);

extern int enablePPPInterface(int cid, const char *user, const char *passwd, char *ipaddress);
extern void disablePPPInterface(int cid);
extern int getInterfaceAddr(int af, const char *ifname, char *ipaddress);
extern void setCPstandby();
void setNetProperty(int cidNum, char *ifname);  

#define PROPERTY_MODEM_PS_STATE "persist.radio.psstate"//add by wangna for psmoving

pthread_mutex_t  pdp_act_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t  pdp_deact_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  pdp_activated = PTHREAD_COND_INITIALIZER;
pthread_cond_t  pdp_deactivated = PTHREAD_COND_INITIALIZER;
static  int pdp_act = 0;
static  int pdp_deact=0;
#define ZTERILD_COPS_PROPERTY              "ril.cops" 
#define ZTERILD_PDP_CGDCONT "ril.cgdcont" //add for pdp cgdcont 051368
#define ZTERILD_APN_PROPERTY  "persist.radio.apn"//add for apn setting
#define ZTERILD_PROTOCOL "persist.radio.protocol"


static pthread_mutex_t  g_datacall_info_mutex = PTHREAD_MUTEX_INITIALIZER;

static void ril_unsol_pdpContextChanged(void *param);
static RIL_Data_Call_Response_v6 * mallocPDPContextList( int n);
static int getPdpContextNum(void);
static int getPdpContextNumForCgev(void);
static void setPdpContextLocalStatus(int cid, char *PDP_type, char *address, char *gateway, char *dns1, char *dns2, char *apn);
static int deactiviatePdp(int cid );
static void resetPdpContextLocalStatus(int cid);

static int check_creg_crerg_2 = 0;


/* 通知 */
#define RIL_PS_NET_NOTICE_INVALID 	-1
#define RIL_PS_NET_NOTICE_ACT 		1   /* 行动 */
#define RIL_PS_NET_NOTICE_DEACT 	0   /* 停止动作 */
#define RIL_PS_NET_NOTICE_MODIFY 	2
#define RIL_PS_NET_NOTICE_REACT 	3

typedef struct
{
	int profile;
	char Apn[128];
}Apn_Profile;

RIL_Data_Call_Response_v6 gPdpContList[MAX_DATA_CALLS] = {{0}} ;
Apn_Profile gApnProfile[MAX_DATA_CALLS] = { {-1, {0}}};

//added  by liutao 20131203 for TSP 7510 modem and dual standby start
void ril_request_set_cgactt_zte(int request, void *data, size_t datalen, RIL_Token token)
{
	char *cmd = NULL;
    int err = 0;
    char* cgatt = ((char**)data)[0];
    asprintf(&cmd, "AT+CGATT=%s", cgatt);
    err = at_send_command_timeout(cmd, NULL,TIMEOUT_CGATT);
    free(cmd);

    ALOGD("ril_request_set_cgactt_zte: SetCGATT,err = %d\n",err);
    if(err < 0){
	    err  = at_send_command_timeout("AT+CGATT=0", NULL,TIMEOUT_CGATT);
        if (err < 0 )
        {
          ALOGD("ril_request_attach_gprs failed, detach error= %d" , err);
        }
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	}
    else
    {
    	
    	if (strcmp(cgatt,"1") == 0)
    	{
			set_modem_ps_state(0);
		}
        RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
    }
}
void ril_request_set_cgsms_zte(int request, void *data, size_t datalen, RIL_Token token)
{
	char *cmd = NULL;
    int err = 0;
    char* cgsms = ((char**)data)[0];
    asprintf(&cmd, "AT+CGSMS=%s", cgsms);
    err = at_send_command(cmd, NULL);
    free(cmd);

    ALOGD("ril_request_set_cgsms_zte: SetCGSMS,err = %d\n",err);
    if(err < 0)
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
}
void ril_request_deactivate_zte(int request, void *data, size_t datalen, RIL_Token token)
{
	char *cmd = NULL;
    int err = 0;
    char* dcta = ((char**)data)[0];
    asprintf(&cmd, "AT^DCTA=%s", dcta);
    err = at_send_command(cmd, NULL);
    free(cmd);
    ALOGD("ril_request_deactivate_zte: SetDCTA,err = %d\n",err);
    if(err < 0)
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    else
        RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
}
//added  by liutao 20131203 for TSP 7510 modem and dual standby end
static int hostonModem(void)
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
	
    ALOGD("hostonModem value:%c \n", value);
    if (value == '0')
    {
        if (write(fd, "1", 2) < 0)
        {
            close(fd);
            return -3;
        }
    }
    close(fd);
    sleep(3);
    return 0;
}

static void dumpPdpContext(int cid)
{
	ALOGD("PdpContext[%d]:%d;%s;active:%d;%s %s:%s;gw:%s;dnses:%s;", 
	cid, gApnProfile[cid-1].profile,gApnProfile[cid-1].Apn,gPdpContList[cid - 1].active, gPdpContList[cid - 1].type, 
	gPdpContList[cid - 1].ifname,gPdpContList[cid - 1].addresses, gPdpContList[cid - 1].gateways, gPdpContList[cid - 1].dnses);
}

static void dumpPdpContextList(void)
{
	int cid;
    for (cid = 0; cid < MAX_DATA_CALLS; cid++)
    {
    	     if(gPdpContList[cid - 1].active == 1)	dumpPdpContext(cid);
    }

}

static void getpValue(char **ppVale, char *out)
{
	char *pVale= NULL;
    if (ppVale != NULL)
    {
        pVale = malloc(strlen(out) + 1);
		memset(pVale,0, strlen(out) + 1);
        strcpy(pVale, out);
        *ppVale = pVale;
    }
}

/* 
 * parse main PDP context list. +CGCONTRDP
 * +CGCONTRDP: <cid>,<bearer_id>,<apn>[,<address>][,<gw_addr>][,<DNS_prim_addr>][,<DNS_sec_addr>][,<P-CSCF_prim_addr>][,<P-CSCF_sec_addr>]   
 * +CGCONTRDP: 1,5,"zx.com.mnc008","10.42.215.8.255.0.0.0",,"10.41.132.9","10.41.132.10",,
 */
static int parse_cgcontrdp(char *line, int *pCid, char **pApn, char **pAddress, char **pGateWay,char **dns1,char **dns2)
{
    int  ignore = 0 ;
	int err = 0;
    char *out = NULL;
	err = at_tok_start(&line);
	DO_ERROR_JDUGE;

	err = at_tok_nextint(&line, pCid);
	DO_ERROR_JDUGE;

	err = at_tok_nextint(&line, &ignore); //<bearer_id>
	DO_ERROR_JDUGE;
	
    err = at_tok_nextstr(&line, &out);//<apn>
	DO_ERROR_JDUGE;
	getpValue(pApn,out);
	
	err = at_tok_nextstr(&line, &out);//<address>
	DO_ERROR_JDUGE;
	getpValue(pAddress,out);
	
    err = at_tok_nextstr(&line, &out); //gw_addr
	DO_ERROR_JDUGE;
	getpValue(pGateWay,out);
	
    err = at_tok_nextstr(&line, &out); //DNS_prim_addr
	DO_ERROR_JDUGE;
	getpValue(dns1,out);

	err = at_tok_nextstr(&line, &out); //DNS_sec_addr
	DO_ERROR_JDUGE;
	getpValue(dns2,out);
    //ALOGD("%s: successful", __FUNCTION__);	
    return err;
error:
    FREE(*pApn);
    FREE(*pAddress);
    FREE(*pGateWay);
    FREE(*dns1);
    FREE(*dns2);
    ALOGD("%s: failed", __FUNCTION__);	
    return err;
}

/* 
 * parse +ZGIPDNS 
   <cid_num>,
   <cid1>,<PDP_type>,<PDP_address>,<gateway>,<primary_dns>,<secondary_dns>[,
   <cid2>,<PDP_type>,<PDP_address>,<gateway>,<primary_dns>,<secondary_dns>[...]] 
*/
static int parse_zgipdns(char *line, int *cid_num, int *cid_parsed, char **PDP_type, char **address, char **gateway, char **dns1, char **dns2)
{
	int err =0;
      char *out = NULL;
      err = at_tok_start(&line);
	DO_ERROR_JDUGE;
	err = at_tok_nextint(&line, cid_num);
	DO_ERROR_JDUGE;
	err = at_tok_nextint(&line, cid_parsed);
	DO_ERROR_JDUGE;
	err = at_tok_nextstr(&line, &out);
	DO_ERROR_JDUGE;
	getpValue(PDP_type,out);
	err = at_tok_nextstr(&line, &out);
	DO_ERROR_JDUGE;
	getpValue(address,out);
	err = at_tok_nextstr(&line, &out);
	DO_ERROR_JDUGE;
	getpValue(gateway,out);
	err = at_tok_nextstr(&line, &out);
	DO_ERROR_JDUGE;
	getpValue(dns1,out);
	err = at_tok_nextstr(&line, &out);
	DO_ERROR_JDUGE;
	getpValue(dns2,out);
      return 0;
      //ALOGD("%s: Successful", __FUNCTION__);
error:
    ALOGD("%s: failed", __FUNCTION__);
    FREE(*PDP_type);
    FREE(*address);
    FREE(*gateway);
    FREE(*dns1);
    FREE(*dns2);
    return -1;
}

/* 
 * parse +CGEV
 * only support ME/NW PDN ACT/DEACT 
 * eg. +CGEV: ME PDN ACT 1
 */
static int parse_cgev(char *line, int * p_cid)
{
	int action = RIL_PS_NET_NOTICE_INVALID;
	if(NULL != strstr(line, "DEACT"))
	{
		action = RIL_PS_NET_NOTICE_DEACT;
		char *p = strstr(line, "ACT");
		p+=4;
		char cid_parsed = *p;
		*p_cid = cid_parsed -'0';		
	}
	else if(NULL != strstr(line, "ACT"))
	{
		action = RIL_PS_NET_NOTICE_ACT;
		char *p = strstr(line, "ACT");
		p+=4;
		char cid_parsed = *p;
		*p_cid = cid_parsed -'0';		
	}
	else
	{
		action = RIL_PS_NET_NOTICE_INVALID;
		*p_cid = 0;
	}
	
    ALOGD("%s: action:%d; cid %d", __FUNCTION__, action,*p_cid);
    return action;
}


/*get gateway address form IP address*/
void getGwFromIP(char *address, char **gw)
{
	if(NULL!=address && NULL!=gw)
	{
		int t;
		char *ph=NULL;
		char *ph1=address;
		int i;
		int count=0;
		char str_ok[30]={0};
		char str[20]={0};
		for(i=1; i < strlen(ph1);i++)
		{
			if(ph1[i]=='.')
			{
				count+=1;
			}
			if(count == 3)
			{
				break;
			}
		}

		strncpy(str,address,i);
		ph=strchr(address,'.');
		ph=strchr(ph+1,'.');
		ph=strchr(ph+1,'.');		
		t= atoi(ph+1);
		
		if((254 == t) || (255==t) )//modified by Boris 20161128
			sprintf(str_ok,"%s.%d",str,t-2);
		else
			sprintf(str_ok,"%s.%d",str,t+1);
		asprintf(gw,"%s",str_ok);
		//ALOGD("%s %s =>%s",__FUNCTION__, address,*gw);
	}
}

//10.41.132.9 10.41.132.10
void setNetProperty(int cid, char *ifname)
{
    char    proper_name[PROPERTY_KEY_MAX] ={0};
    int ret;

    if (gPdpContList[cid - 1].dnses != NULL )
    {
    	char Dns[PROPERTY_VALUE_MAX] = {0};
		char* pdestaddr = Dns;
		char* pSrcaddr = gPdpContList[cid - 1].dnses;
		while(pSrcaddr!=' ')
		{
			//ALOGD("%s:%s",__FUNCTION__,Dns);
			if(*pSrcaddr == ' ') break;
			*pdestaddr = *pSrcaddr;
			pSrcaddr++;
			pdestaddr++;		
		}
		
		sprintf(proper_name, "net.%s.dns1", ifname);
        ret = property_set(proper_name, Dns);
	if(ret !=0)   ALOGD("property_set %s:%s;ret:%d\n", proper_name, Dns, ret);

		pSrcaddr++;
        sprintf(proper_name, "net.%s.dns2", ifname);
        ret = property_set(proper_name, pSrcaddr);
	if(ret !=0)	ALOGD("property_set %s:%s;ret:%d\n", proper_name, pSrcaddr, ret);
    }
	
    if (gPdpContList[cid - 1].gateways != NULL)
    {
        sprintf(proper_name, "net.%s.gw", ifname);
        ret = property_set(proper_name, gPdpContList[cid - 1].gateways);
        if(ret !=0) ALOGD("property_set %s:%s;ret:%d\n", proper_name, gPdpContList[cid - 1].gateways, ret);
    }

    return;
}

void resetNetProperty(int cid, char *ifname)
{
    char    proper_name[PROPERTY_KEY_MAX] ={0};
    int ret;
	sprintf(proper_name, "net.%s.gw", gPdpContList[cid - 1].ifname);
	property_set(proper_name, "");

	sprintf(proper_name, "net.%s.dns1", gPdpContList[cid - 1].ifname);
	property_set(proper_name, "");

	sprintf(proper_name, "net.%s.dns2", gPdpContList[cid - 1].ifname);
	property_set(proper_name, "");

    return;
}


static int allocCid(void)
{
	int cid = 1;
	if (gPdpContList[0].active ==1 )
	{
		return 2;
	}
	else
	{
		return 1;
	}
#if 0	
   	for(cid =1;cid<=MAX_DATA_CALLS;cid++)
   	{
	    if (gPdpContList[cid - 1].active !=1  ) 
	    {
		  ALOGD("allocCid cid:%d\n", cid);
		  return cid;
	    }
	}
#endif	
	return 0;
}

static bool checkProfile (int profile)
{
	int cid = 0;
   	for(cid =0;cid<MAX_DATA_CALLS;cid++)
   	{
	    if (gApnProfile[cid - 1].profile == profile  ) 
	    {
		  return true;
	    }
	}
	return false;
}

static int definePdpContext (int profile, int cid, const char *apn, const char *protocol)
{
    char    cmdString[MAX_AT_LENGTH] = { 0 };
    ATResponse  *p_response = NULL;

    if(profile ==2) //IMS
    {
    	sprintf(cmdString, "AT+CGDCONT=%d,\"%s\",\"%s\",,,,,0,1,1", cid, protocol, apn);
    }
    else
    {
     	sprintf(cmdString, "AT+CGDCONT=%d,\"%s\",\"%s\"", cid, protocol, apn);
    }
    int err = at_send_command(cmdString, &p_response);
    if (err < 0 || p_response->success == 0)
    {
        ALOGD("Define pdp cid %d failed", cid);
		err =-1;
    }
	else
	{
		ALOGD("Define pdp cid %d successfully", cid);
	}
	at_response_free(p_response);
	return err;
}

static int detachLte(void)
{
	
	char	cmdString[MAX_AT_LENGTH] = { 0	  };
	ATResponse	*response = NULL;
	int err = -1;
    sprintf(cmdString, "AT+CGATT=0");
    err = at_send_command_timeout(cmdString, &response,TIMEOUT_CGATT);
    if (err < 0 || response->success == 0)
    {
		ALOGD("%s: failed", __FUNCTION__);
		err =-1;
    }
	else
	{
		ALOGD("%s: success", __FUNCTION__);
	}
	at_response_free(response);
	return err;	
}


static int attachLte(void)
{
	
	char	cmdString[MAX_AT_LENGTH] = { 0	  };
	ATResponse	*response = NULL;
	int err = -1;
    sprintf(cmdString, "AT+CGATT=1");
    err = at_send_command_timeout(cmdString, &response,TIMEOUT_CGATT);
    if (err < 0 || response->success == 0)
    {
		ALOGD("%s: failed", __FUNCTION__);
		err =-1;
    }
	else
	{
		ALOGD("%s: success", __FUNCTION__);
	}
	at_response_free(response);
	return err;	
}


static int defineLteSecPdpContext(int cid)
{
	
	char	cmdString[MAX_AT_LENGTH] = { 0	  };
	ATResponse	*p_response = NULL;
	int err = -1;
    sprintf(cmdString, "AT+CGCONTRDP=%d", cid);
    err = at_send_command_timeout(cmdString, &p_response, TIMEOUT_CGDATA);
    if (err < 0 || p_response->success == 0)
    {
		ALOGD("define Second pdp cid %d to data comm failed", cid);
		err =-1;
    }
	else
	{
		ALOGD("define Second cid %d to data comm successfully", cid);
	}
	at_response_free(p_response);
	return err;	
}

static int check_sysinfo( int sysinfo[] )
{
	ATResponse  *response = NULL;
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
    err = at_tok_nextint(&line, &(sysinfo[3])); //17-4g 3-2g  5、15 - 3g
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

static int check_cereg_mode(char* cereg_result, int responseInt[])
{
	int i;
	int temp_cereg_str[50] = {0};

	if( cereg_result != NULL ) {

		for(i=0;i < strlen(cereg_result);i++)
		{
			temp_cereg_str[i]=cereg_result[i];
			//ALOGD(" %c ",temp_cereg_str[i]);

		}
		if ( i > 2 ) {
			ALOGD(" ==== temp_cereg_str[i-3] <bufantong> = %d  ",temp_cereg_str[i-3]);
			responseInt[0] = temp_cereg_str[i-3];
			responseInt[1] = temp_cereg_str[i-1];  //2g creg
			return 1;
		}		
	} else if ( NULL == cereg_result ) {
		return 0;
	}
	return 0;
}

static int check_creg(void)
{
	ALOGD(" ==== check_creg <bufantong> ");
	int responseInt[10] = {9,9,9,9,9,9,9,9,9,9};
	int err, num = 0;
	ATResponse  *response = NULL;
	int sysinfo[4] = {0}; //bufantong 20170110
	char *line;
	
	err = check_sysinfo( sysinfo );
	ALOGD(" ====  <bufantong> sys_mode =%d",sysinfo[3] );
	
	usleep(100*1000);

	if(sysinfo[3] == 17)   //bft 20170110
    {
    	ALOGD(" ==== 111 <bufantong> ");
    	err = at_send_command_singleline("AT+CEREG?", "+CEREG:", &response);
		line = response->p_intermediates->line;
	    err = at_tok_start(&line);
		ALOGD("line %s",line);
		if (err < 0)
			return 0;
		err = check_cereg_mode(line,responseInt);

		#if 0
		err = at_tok_nextint(&line, &(responseInt[0]));
		if (err < 0)
			return 0;
		err = at_tok_nextint(&line, &(responseInt[1]));
		if (err < 0)
			return 0;
		#endif
		ALOGD(" ==== <bufantong> cereg responseInt = %d  ",responseInt[0]);
		//if ( (responseInt[0] != 1) && (responseInt[0] != 5) ) //1已注册 5注册漫游
		if ( (responseInt[0] != 55) )  //字符7
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
    }
	#if 1
    else
    {
    	ALOGD(" ==== 222 <bufantong> ");
    	err = at_send_command_singleline("AT+CREG?", "+CREG:", &response);
		line = response->p_intermediates->line;
	    err = at_tok_start(&line);
		//ALOGD("line %s",line);
		if (err < 0)
			return 0;
		err = check_cereg_mode(line,responseInt);

		#if 0
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
			if (err < 0)
				return 0;
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
		#endif
		//ALOGD(" ==== creg1 responseInt = %d <bufantong> ",responseInt[1]);
		//ALOGD(" ==== creg2 responseInt = %d <bufantong> ",responseInt[0]);
		//if ( (responseInt[1] != 1) && (responseInt[1] != 5) ) //1已注册 5注册漫游
		if ( (responseInt[1] != 51) && (responseInt[0] != 52) && (responseInt[1] != 48) )  //字符3-51 4-52
	    {
	    	ALOGD(" ==== 666 <bufantong> ");
	        system("echo 0 > /sys/class/lc4g_ctrl/lc4g_ctrl/enable");//bufantong 20170110
			sleep(2);
			system("echo 1 > /sys/class/lc4g_ctrl/lc4g_ctrl/enable");

			at_close_fds();
			//find_available_dev();
			kill(getpid(), 10);
	    }
		ALOGD(" ==== 333 <bufantong> ");
	}
	#endif
	return 1;
}

void syncPdpContext(int cid)
{
    char *dns1 = NULL, *dns2 = NULL,*address = NULL, *gateway = NULL,*apn = NULL;
    int  cid_parsed;
    char cmdString[MAX_AT_LENGTH] = { 0	  };
    ATResponse	*response = NULL;
       sprintf(cmdString, "AT+CGCONTRDP=%d", cid);
	int err = at_send_command_singleline(cmdString, "+CGCONTRDP",&response);
	DO_RESPONSE_JDUGE;
	err = parse_cgcontrdp(response->p_intermediates->line, &cid_parsed, &apn, &address, &gateway, &dns1, &dns2);
	DO_ERROR_JDUGE;

	/*  */
	resetPdpContextLocalStatus(cid_parsed);
	setPdpContextLocalStatus(cid_parsed, NULL, address, gateway, dns1, dns2, apn);
	ALOGD("%s:successful", __FUNCTION__);


/* bufantong 2017 01 10 */
#if 0
//#ifdef CHECK_CREG_CEREG
		if ( check_creg_crerg_2 == 0 ) {
			sleep(2);
			err = check_creg();
			check_creg_crerg_2 = 1;
		}
#endif
	goto exit;
error:
    	ALOGD("%s: failed", __FUNCTION__);
exit:	
	setCPstandby();
	at_response_free(response);
	FREE(address);
	FREE(gateway);
	FREE(apn);
	FREE(dns1);
	FREE(dns2);
}

RIL_Data_Call_Response_v6* allocPDPContextList( int num )
{
	RIL_Data_Call_Response_v6  *pdpResponses = malloc(num * sizeof(RIL_Data_Call_Response_v6));
	assert(pdpResponses != NULL);
	memset(pdpResponses,0,num * sizeof(RIL_Data_Call_Response_v6));
	return pdpResponses;
}

static void freePDPContextList(RIL_Data_Call_Response_v6 *pdpResponses, int num)
{
	int i=0;
    RIL_Data_Call_Response_v6 *pdpResponsesTemp = pdpResponses ;
    for (i = 0; i < num; i++)
    {	
		ALOGD("freePDPContextList %d %x,%x\n", i,pdpResponsesTemp,pdpResponses);
    	FREE(pdpResponsesTemp->addresses);
		FREE(pdpResponsesTemp->dnses);
		FREE(pdpResponsesTemp->gateways);
		FREE(pdpResponsesTemp->ifname);
		FREE(pdpResponsesTemp->type);
		pdpResponsesTemp ++ ; 
    }
	FREE(pdpResponses);
}

void syncPdpContextList(RIL_Data_Call_Response_v6* pdpResponses, int num )
{
	int cid = 0;
	int index = 0;
	RIL_Data_Call_Response_v6 *pdpResponsesTemp = pdpResponses;
	for(cid=0; cid< MAX_DATA_CALLS; cid++) 
	{
		if((gPdpContList[cid].active ==1)&& (index < num))
		{
			memcpy(pdpResponsesTemp,&(gPdpContList[cid]),sizeof(RIL_Data_Call_Response_v6));
			pdpResponsesTemp->addresses = strdup(gPdpContList[cid].addresses);
			pdpResponsesTemp->dnses = strdup(gPdpContList[cid].dnses);
			pdpResponsesTemp->gateways = strdup(gPdpContList[cid].gateways);
			pdpResponsesTemp->ifname = strdup(gPdpContList[cid].ifname);
			pdpResponsesTemp->type = strdup(gPdpContList[cid].type);
			
			pdpResponsesTemp++;
			index++;
		}
	}
	return;
}


static int setPdpfilter(int cid)
{
	
	char	cmdString[MAX_AT_LENGTH] = { 0	  };
	ATResponse	*p_response = NULL;
	int err = -1;
    sprintf(cmdString, "AT+CGTFTRDP=%d,%d", 1, cid);
    err = at_send_command_timeout(cmdString, &p_response, TIMEOUT_CGDATA);
    if (err < 0 || p_response->success == 0)
    {
		ALOGD("define Second pdp cid %d to data comm failed", cid);
		err =-1;
    }
	else
	{
		ALOGD("define Second cid %d to data comm successfully", cid);
	}
	at_response_free(p_response);
	return err;	
}

static int activatePdp(int cid )
{
    char    cmdString[MAX_AT_LENGTH] = { 0 };
    ATResponse  *p_response = NULL;
    sprintf(cmdString, "AT+CGACT=1,%d", cid);
    int err = at_send_command_timeout(cmdString, &p_response, TIMEOUT_CGACT_DEACT);
    if (err < 0 || p_response->success == 0)
    {
        ALOGD("Activiate pdp cid %d failed", cid);
	 err =-1;
    }
	else
	{
		ALOGD("Activiate pdp cid %d successfully", cid);
    }
	at_response_free(p_response);
	return err;
}

static int deactivatePdp(int cid)
{
    char    cmdString[MAX_AT_LENGTH] = { 0    };
    ATResponse  *p_response = NULL;
    sprintf(cmdString, "AT+CGACT=0,%d", cid);
    int err = at_send_command_timeout(cmdString, &p_response, TIMEOUT_CGACT_DEACT);
    if (err < 0 || p_response->success == 0)
    {
        ALOGD("Deactiviate pdp cid %d failed", cid);
	err =-1;
    }
	else
	{
		ALOGD("Deactiviate pdp cid %d successfully", cid);
    }
	at_response_free(p_response);
	return err;	
}

static int activiatePpp(int cid, const char *apn, const char *protocol)
{
	char	cmdString[MAX_AT_LENGTH] = { 0	  };
	ATResponse	*p_response = NULL;
	sprintf(cmdString, "ATD*98*1#");
	int err = at_send_command_timeout_ps(3, cmdString, &p_response, TIMEOUT_CGDATA);
	if (err < 0 || p_response->success == 0)
	{
        ALOGD("activiatePpp failed");
		err =-1;
	}
	else
	{
	    ALOGD("activiatePpp successfully");
	}
	at_response_free(p_response);
	return err;
}

static int deactiviatePpp(int cid, const char *apn, const char *protocol)
{
	char	cmdString[MAX_AT_LENGTH] = { 0	  };
	ATResponse	*p_response = NULL;
	sprintf(cmdString, "ATD*98*1#");
	int err = at_send_command_timeout_ps(3, cmdString, &p_response, TIMEOUT_CGDATA);
	if (err < 0 || p_response->success == 0)
	{
        ALOGD("activiatePpp failed");
		err =-1;
	}
	else
	{
	    ALOGD("activiatePpp successfully");
	}
	at_response_free(p_response);
	return err;
}

static int deactivateRndisInterface(int cid)
{
	char	cmdString[MAX_AT_LENGTH] = { 0	  };
	ATResponse	*p_response = NULL;
    sprintf(cmdString, "AT+ZGACT=0,%d", cid);
    int err = at_send_command_timeout(cmdString, &p_response, TIMEOUT_ZGACT);
    if (err < 0 || p_response->success == 0)
    {
		ALOGD("deactivateRndisInterface cid %d failed", cid);
		err =-1;
    }
	else
	{
		ALOGD("deactivateRndisInterface cid %d successfully", cid);
	}
	at_response_free(p_response);
	return err; 
}

static int activateRndisInterface(int cid)
{
	char	cmdString[MAX_AT_LENGTH] = { 0	  };
	ATResponse	*p_response = NULL;
    sprintf(cmdString, "AT+ZGACT=1,%d", cid);
    int err = at_send_command_timeout(cmdString, &p_response, TIMEOUT_ZGACT);
    if (err < 0 || p_response->success == 0)
    {
		ALOGD("activateRndisInterface cid %d failed", cid);
		err =-1;
    }
	else
	{
		ALOGD("activateRndisInterface cid %d successfully", cid);
	}
	at_response_free(p_response);
	return err; 
	
}

//add for get global IPV6
#define SUPPORT_IPV6  1
#ifdef SUPPORT_IPV6
typedef enum 
{
	PROTOCOL_IPV4,
	PROTOCOL_IPV6,
	PROTOCOL_IPV4V6
}Protocol_Type;

typedef enum
{
	PROSUPPORT_NONE = 1 << 0,
	PROSUPPORT_IPV4 = 1 << 1,
	PROSUPPORT_IPV6 = 1 << 2
    //SUPPORT_IPV4V6 = 1 << 3
}ProSupportType;


/* disable the forwarding to send RS and not set the addr when receive ra packet */
static int modifyIpv6conf(char *ifname)
{
	char   cmdString[256] = { 0	  };
    int    fd = -1;
    char   value = 0;

    fd = open("/proc/sys/net/ipv6/conf/all/forwarding", O_RDWR);
    if (fd < 0)
    {
        ALOGD("forwarding open error:%s \n", strerror(errno));
		return -1;
    }

    if (read(fd, &value, 1) != 1)
    {
        ALOGD("forwarding read error:%s \n", strerror(errno));
		close(fd);
        return -2;
    }
	
    ALOGD("forwarding value:%c \n", value);
    if (value != '0')
    {
        if (write(fd, "0\n", 2) < 0)
        {
            ALOGD("forwarding write error:%s \n", strerror(errno));
			close(fd);
            return -3;
        }
    }
    close(fd);

	sprintf(cmdString, "/proc/sys/net/ipv6/conf/%s/accept_ra",ifname);
	fd = open(cmdString, O_RDWR);
    if (fd < 0)
    {
        ALOGD("accept_ra open error:%s \n", strerror(errno));
		return -1;
    }

    if (read(fd, &value, 1) != 1)
    {
        ALOGD("accept_ra read error:%s \n", strerror(errno));
		close(fd);
        return -2;
    }
	
    ALOGD("accept_ra value:%c \n", value);
    if (value != '0')
    {
        if (write(fd, "0\n", 2) < 0)
        {
            ALOGD("accept_ra write error:%s \n", strerror(errno));
			close(fd);
            return -3;
        }
    }
    close(fd);
	
	sprintf(cmdString, "/proc/sys/net/ipv6/conf/%s/router_solicitations",ifname);
	fd = open(cmdString, O_RDWR);
    if (fd < 0)
    {
        ALOGD("router_solicitations open error:%s \n", strerror(errno));
		return -1;
    }

    if (read(fd, &value, 1) != 1)
    {
        ALOGD("router_solicitations read error:%s \n", strerror(errno));
		close(fd);
        return -2;
    }
	
    ALOGD("router_solicitations value:%c \n", value);
    if (value != '0')
    {
        if (write(fd, "0\n", 2) < 0)
        {
            ALOGD("router_solicitations write error:%s \n", strerror(errno));
			close(fd);
            return -3;
        }
    }
    close(fd);
	
    return 0;
}

static void startZteIpv6Slaac(char* ifname)
{
	/* zhangfen */
	char	cmdString[256] = {0};
	int     ret = -1;
	
	// modifyIpv6conf(ifname);    
	
	/*
	sprintf(cmdString, "zte_ipv6_slaac -i %s", ifname);
	ret = system(cmdString);
	ALOGD("exec cmd scd: %s and ret is: %d!", cmdString, ret);
	*/
	
	/* shengchundong begin*/
	ALOGD("startZteIpv6Slaac");
	property_set("ctl.start", "ipv6_slaac");

	//sleep(3); 

}

//254.128.0.0.0.0.0.0.0.0.0.0.0.0.0.0 --> FE:80:0:0:0:0:0:0
void ipv6addrconvert(char *srcipv6, char *dstipv6)
{
	unsigned int ipv6_addr[16] = {0};
	char  ipv6address[100]     = {0};
	struct in6_addr addr       = {0};
	int size = INET6_ADDRSTRLEN + 1;
	int   i = 0;
	
	sscanf(srcipv6, "%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d.%d",
		ipv6_addr, ipv6_addr+1,ipv6_addr+2,ipv6_addr+3,
		ipv6_addr+4,ipv6_addr+5,ipv6_addr+6,ipv6_addr+7,
		ipv6_addr+8,ipv6_addr+9,ipv6_addr+10,ipv6_addr+11,
		ipv6_addr+12,ipv6_addr+13,ipv6_addr+14,ipv6_addr+15);

	for(i = 0; i <= 15; i++)
	{
		sprintf(ipv6address+strlen(ipv6address), "%02x", ipv6_addr[i]);
		if((i%2 == 1)&&(i < 15))
		{
			sprintf(ipv6address+strlen(ipv6address), ":");
		}
	}
	
	ALOGD("ipv6addrconvert: srcipv6=%s dstipv6=%s!", srcipv6, ipv6address);

    if (inet_pton(AF_INET6, ipv6address, &addr) != 1)
    {
        ALOGE("inet_pton failed");
    }
	if(inet_ntop(AF_INET6, addr.s6_addr, dstipv6, size) == NULL)
	{
        ALOGE("inet_ntop failed");
    }

	ALOGD("ipv6addrconvert: srcipv6=%s dstipv6=%s!", srcipv6, dstipv6);
	
}


int parseV4addrgw(char *address, char *ipaddr, char *ipgw)
{
	char *pstart = address;
	char *ptemp  = NULL;
	char iptemp[INET_ADDRSTRLEN] = {0};
	int  temp = 0;
	int  i = 0;

	if(address == NULL | ipaddr == NULL | ipgw == NULL)
		return -1;
	
	for(i = 0; i < 4; i++)
	{
		ptemp = strchr(pstart, '.');
		if(NULL == ptemp)
		{
			return -1;
		}
		pstart = ptemp + 1;
	}

	pstart--;
	strncpy(ipaddr, address, (pstart - address));

	ALOGD("parseV4addrgw: addr =%s", ipaddr);

	pstart = ipaddr;
	for(i = 0; i < 3; i++)
	{
		ptemp = strchr(pstart, '.');
		if(NULL == ptemp)
		{
			return -1;
		}
		pstart = ptemp + 1;
	}
	pstart--;
	strncpy(iptemp, ipaddr, (pstart - ipaddr));
	temp = atoi(pstart+1);
	sprintf(ipgw,"%s.%d",iptemp,(temp+1) > 255 ? (temp -1) : (temp+1));

	ALOGD("parseV4addrgw: gw =%s", ipgw);

	return 0;
}

static void setPdpV4V6ContextLocalStatus(int cid, Protocol_Type PDP_type, char *address, char *gateway, char *dns1v4, char *dns1v6, char *dns2v4, char *dns2v6, char *apn)
{
	pthread_mutex_lock(&g_datacall_info_mutex); 		
	gPdpContList[cid - 1].cid = cid;
	gPdpContList[cid - 1].active = 1;
	gPdpContList[cid - 1].status = PDP_FAIL_NONE;
	gPdpContList[cid - 1].suggestedRetryTime = 0;//0x7fffffff;

	asprintf(&(gPdpContList[cid - 1].type),"%s","IPV4V6");
		
	asprintf(&(gPdpContList[cid - 1].addresses),"%s",address);
	asprintf(&(gPdpContList[cid - 1].gateways), "%s",gateway);	
	asprintf(&(gPdpContList[cid - 1].dnses), "%s %s %s %s", dns1v4,dns2v4,dns1v6,dns2v6);
	if(apn != NULL)
	{
		strncpy(gApnProfile[cid - 1].Apn,apn,128);
	}
	dumpPdpContext(cid);
	pthread_mutex_unlock(&g_datacall_info_mutex);
	//ALOGD("%s:successful", __FUNCTION__);

}

static void setPdpV6ContextLocalStatus(int cid, Protocol_Type PDP_type, char *address, char *gateway, char *dns1, char *dns2, char *apn)
{

	pthread_mutex_lock(&g_datacall_info_mutex); 		
	gPdpContList[cid - 1].cid = cid;
	gPdpContList[cid - 1].active = 1;
	gPdpContList[cid - 1].status = PDP_FAIL_NONE;
	gPdpContList[cid - 1].suggestedRetryTime = 0;//0x7fffffff;

	if( PDP_type = PROTOCOL_IPV6)
	{
		asprintf(&(gPdpContList[cid - 1].type),"%s", "IPV6");
	}
	else if( PDP_type = PROTOCOL_IPV4V6)
	{
		asprintf(&(gPdpContList[cid - 1].type),"%s","IPV4V6");
	}
	else
	{
		asprintf(&(gPdpContList[cid - 1].type),"%s", "IPV4");
	}
	
	asprintf(&(gPdpContList[cid - 1].addresses),"%s",address);
	asprintf(&(gPdpContList[cid - 1].gateways), "%s",gateway);	
	asprintf(&(gPdpContList[cid - 1].dnses), "%s %s", dns1,dns2);
	//asprintf(&(gPdpContList[cid - 1].ifname), "usb%d", cid-1);
	if(apn != NULL)
	{
		strncpy(gApnProfile[cid - 1].Apn,apn,128);
	}
	dumpPdpContext(cid);
	pthread_mutex_unlock(&g_datacall_info_mutex);
	//ALOGD("%s:successful", __FUNCTION__);
}

bool isIPv4(char *addr)
{	
	char* pSrcaddr = addr;
	int count = 0;
	
	while(*pSrcaddr != '\0')
	{		
		if(*pSrcaddr == '.') 
			count++;
				
		pSrcaddr++;
	}

	if(count > 8)
		return false;
	else
		return true;
}

void freeresponse(char *apn, char *addr, char *gw, char *dns1, char *dns2)
{
	if(apn != NULL)
		FREE(apn);
	if(addr != NULL)
		FREE(addr);
	if(gw != NULL)
		FREE(gw);
	if(dns1 != NULL)
		FREE(dns1);
	if(dns2 != NULL)
		FREE(dns2);

}

void syncPdpV6Context(int cid, Protocol_Type pdptype)
{
    char *dns1 = NULL, *dns2 = NULL,*address = NULL, *gateway = NULL,*apn = NULL;
    int  cid_parsed = -1;
    char cmdString[MAX_AT_LENGTH] = { 0	  };
    ATResponse	*response = NULL;
	ATLine *p_cur;
	char  ipaddr[INET_ADDRSTRLEN + INET6_ADDRSTRLEN +1]  = {0};
	char  gwaddr[INET_ADDRSTRLEN + INET6_ADDRSTRLEN +1]    = {0};
	char  ipv4addr[INET_ADDRSTRLEN + 1] = {0};
	char  ipv4gw[INET_ADDRSTRLEN + 1]   = {0};
	char  ipv6addr[INET6_ADDRSTRLEN + 1] = {0};
	char  ipv6gw[INET6_ADDRSTRLEN + 1]   = {0};
	char  ipv4dns1[INET_ADDRSTRLEN + 1]  = {0};
	char  ipv4dns2[INET_ADDRSTRLEN + 1]  = {0};
	char  ipv6dns1[INET6_ADDRSTRLEN + 1] = {0};
	char  ipv6dns2[INET6_ADDRSTRLEN + 1] = {0};
	int err = -1;
	ProSupportType supportType = PROSUPPORT_NONE;

	sprintf(cmdString, "AT+CGCONTRDP=%d", cid);
	err = at_send_command_multiline(cmdString, "+CGCONTRDP",&response);

	DO_RESPONSE_JDUGE;
   
    for (p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next) 
	{
        char *line = p_cur->line;
        int cid;

		freeresponse(apn, address, gateway, dns1, dns2);
		
		err = parse_cgcontrdp(line, &cid_parsed, &apn, &address, &gateway, &dns1, &dns2);
		DO_ERROR_JDUGE;
		
		if(isIPv4(address))
		{
			supportType |= PROSUPPORT_IPV4;
			strncpy(ipv4dns1, dns1, strlen(dns1));
			strncpy(ipv4dns2, dns2, strlen(dns2));
			parseV4addrgw(address, ipv4addr, ipv4gw);	
		}
		else
		{
			supportType |= PROSUPPORT_IPV6;
			ipv6addrconvert(dns1, ipv6dns1);			
			ipv6addrconvert(dns2, ipv6dns2);	
		} 
    }
	
	resetPdpContextLocalStatus(cid_parsed);

    //20151109 majun mod for ECM netcard 
    asprintf(&(gPdpContList[cid - 1].ifname), "%s%d", DEV_NETCARD, cid - 1);
	//asprintf(&(gPdpContList[cid - 1].ifname), "%s%d", DEV_NETCARD, 0); //bufantong 20170105

	if(supportType & PROSUPPORT_IPV6)
	{
		/* start slaac begin*/
#if 1
	    modifyIpv6conf(gPdpContList[cid - 1].ifname);
		upInterface(gPdpContList[cid - 1].ifname);
		sleep(1);
		startZteIpv6Slaac(gPdpContList[cid - 1].ifname);
#endif
		/* slaac end*/
		
		//get ipv6 and gw
		getInterfaceip6gw(gPdpContList[cid - 1].ifname, ipv6addr, ipv6gw);
	}

	if((supportType & PROSUPPORT_IPV6)&&(supportType & PROSUPPORT_IPV4))
	{
		sprintf(ipaddr,"%s %s",ipv4addr, ipv6addr);
		sprintf(gwaddr,"%s %s",ipv4gw,   ipv6gw);

		ALOGD("syncPdpV6Context: ipaddr=%s gwaddr=%s!", ipaddr, gwaddr);
		setPdpV4V6ContextLocalStatus(cid_parsed, pdptype,ipaddr,gwaddr,ipv4dns1,ipv6dns1,ipv4dns2,ipv6dns2, apn);
		//IPV6在SLAAC中配置接口
		configureInterface4(ipv4addr, gPdpContList[cid - 1].ifname);

	}
	else if(supportType & PROSUPPORT_IPV6)
	{
		setPdpV6ContextLocalStatus(cid_parsed, pdptype, ipv6addr, ipv6gw, ipv6dns1, ipv6dns2, apn);
		//IPV6在SLAAC中配置接口
	}
	else if(supportType & PROSUPPORT_IPV4)
	{
		setPdpV6ContextLocalStatus(cid_parsed, pdptype, ipv4addr, ipv4gw, ipv4dns1, ipv4dns2, apn);
		configureInterface4(ipv4addr, gPdpContList[cid - 1].ifname);
	}
	else
	{
		ALOGD("%s:supportType = %d", __FUNCTION__, supportType);
	}
	ALOGD("%s:successful", __FUNCTION__);
	goto exit;
error:
    	ALOGD("%s: failed", __FUNCTION__);
exit:	
	setCPstandby();
	at_response_free(response);
	FREE(address);
	FREE(gateway);
	FREE(apn);
	FREE(dns1);
	FREE(dns2);
}

#endif
/*add by 051368 for pdp syn start*/
void synPdpV4V6Context(int cid)
{
#ifdef SUPPORT_IPV6
    char protocol[PROPERTY_VALUE_MAX];
    Protocol_Type pdptype = -1;
    property_get(ZTERILD_PROTOCOL, protocol, "IPV4V6");
	
	if(strcmp(protocol,"IPV6") == 0 )
	{
		pdptype = PROTOCOL_IPV6;
	}
	else if(strcmp(protocol,"IPV4V6") == 0)
	{
		pdptype = PROTOCOL_IPV4V6;
	}else
	{
		pdptype = PROTOCOL_IPV4;
	}

	if(pdptype == PROTOCOL_IPV6 || pdptype == PROTOCOL_IPV4V6)
	{
		syncPdpV6Context(cid, pdptype);
	}
	else
	{
		syncPdpContext(cid);
		configureInterface(gPdpContList[cid - 1].ifname,gPdpContList[cid - 1].addresses);
	}
#else

	syncPdpContext(cid);
#endif
//	RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED, &(gPdpContList[0]), sizeof(RIL_Data_Call_Response_v6));

}
/*add by 051368 for pdp syn end*/

static void setupPppConnection(RIL_Token token, int profile, const char *apn, int auth_type, const char *user,const char *passwd, const char *protocol)
{
    RIL_Data_Call_Response_v6  result =  {  0 };
    char    ipaddress[64] = {  0 };
    int err = 0 ;
    int cid = profile;
	
    err = activiatePpp( cid, apn, protocol);

    err = enablePPPInterface(cid, user, passwd, ipaddress);

	int af = strcasecmp(protocol, "IPv6") ? AF_INET : AF_INET6;
	getInterfaceAddr(af, result.ifname, ipaddress);

    setNetProperty(cid, "ppp0");
    configureInterface("ppp0",ipaddress);

	
}

/*20151109 majun mod for YF*/
static void setupEthConnection(RIL_Token token, int profile, const char *apn, int auth_type, const char *user,const char *passwd, const char *protocol, const char *apn_type)
{

	int err =0;
	Protocol_Type pdptype = -1;
	
#ifdef V2
	int cid = 1;  //bufantong 20170105
#else
	int cid = allocCid();
#endif
	
	
	int result = -1;
	/*20151109 majun delete for original android 4.4 begin
	//added by zhangzhiliang, 20140304, for getting cid when defining pdp context start
	if (strcmp(apn_type, "mms") == 0 || strcmp(apn_type, "supl") == 0){
		cid = 2;
	} 
	//EC: 617002786949
	//updated by zhangzhiliang, 20140609, for apn type "default" or "*", allocate cid 1,
	//otherwise, non "cmnet" apn get cid 2 allocated after ps transfer, as cid 1 is active after syncPdpContext
	else if (strcmp(apn_type, "default") == 0 || strcmp(apn_type, "*") == 0) {
		cid = 1;
	}
	//added by zhangzhiliang, 20140304, for getting cid when defining pdp context end

	if(1 == cid)
	{
		ALOGI("setupDataOnReadyApns: set isPdpSetup =1!!!");
		property_set("ril.pdpsetup", "1");
		ALOGI("setupDataOnReadyApns: set isPdpSetup =1  over!!!");
	}
	20151109 majun delete for original android 4.4 end*/
		
	/*20151109 majun delete for YF begin
	 *add by 051368 for  PDP set cond start*
	if(1 == cid)
	{
		char mode[PROPERTY_VALUE_MAX];
   		property_get(ZTERILD_PDP_CGDCONT, mode, "0");
   		if(0 == strcmp(mode,"1"))
   		{    ALOGI("get cgdcont 1");
			synPdpV4V6Context(cid);
			RIL_onRequestComplete(token, RIL_E_SUCCESS, &(gPdpContList[cid - 1]), sizeof(RIL_Data_Call_Response_v6));
			property_set("ril.pdpsetup", "0");
	   		return; 
   		}
	}
	*add by 051368 for  PDP set cond end*
	20151109 majun delete for YF end*/
	
	
	strncpy(gApnProfile[cid - 1].Apn, apn, 128);
	gApnProfile[cid - 1].profile = profile;

	if( getCurNwMode() ==17 && cid ==1)
	//if( getCurNwMode() ==17 )  //bufantong 20170105
	{
	    //property_set(ZTERILD_COPS_PROPERTY, "0");	
		/*20151109 majun del for : LTE下不能去激活最后一个PDN,否则造成掉网*/
		//detachLte();
		err = definePdpContext ( profile, cid, apn, protocol);
		property_set(ZTERILD_APN_PROPERTY, apn);
		property_set(ZTERILD_PROTOCOL, protocol); //add by 051368 for pdp syn
		//attachLte();
		//sleep(1);
		//property_set(ZTERILD_COPS_PROPERTY, "1");
	}
	else
	{
		err = definePdpContext ( profile, cid, apn, protocol);
		property_set(ZTERILD_APN_PROPERTY, apn);
		property_set(ZTERILD_PROTOCOL, protocol); //add by 051368 for pdp syn
	}
	
	//DO_ERROR_JDUGE;

	err = activatePdp ( cid);
	DO_ERROR_JDUGE; //modified by Boris for pdp failure bug 20160908 
	
	pthread_mutex_lock(&pdp_act_mutex);
	while(pdp_act !=1)
	{
		pthread_cond_wait(&pdp_activated, &pdp_act_mutex);
	}
	pdp_act =0;
	pthread_mutex_unlock(&pdp_act_mutex);

	err = activateRndisInterface(cid);
	//DO_ERROR_JDUGE;
      	set_modem_ps_state(0);

#ifdef SUPPORT_IPV6
	if(strcmp(protocol,"IPV6") == 0 )
	{
		pdptype = PROTOCOL_IPV6;
	}
	else if(strcmp(protocol,"IPV4V6") == 0)
	{
		pdptype = PROTOCOL_IPV4V6;
	}else
	{
		pdptype = PROTOCOL_IPV4;
	}

	if(pdptype == PROTOCOL_IPV6 || pdptype == PROTOCOL_IPV4V6)
	{
		syncPdpV6Context(cid, pdptype);
	}
	else
	{
		syncPdpContext(cid);
		//configureInterface(gPdpContList[0].ifname,gPdpContList[cid - 1].addresses);//add by wangna //bufantong 20170104
		configureInterface(gPdpContList[cid - 1].ifname,gPdpContList[cid - 1].addresses);//add by wangna
	}
#else

	syncPdpContext(cid);
#endif

    if (gPdpContList[cid - 1].active == 1)
    {
        /*20151109 majun del for useless code*/
    	//	hostonModem();
		/*20151109 majun del for 可能会造成网卡down*/
		//setNetProperty(cid,gPdpContList[cid - 1].ifname);
#ifdef SUPPORT_IPV6	
#if 0
		if(pdptype == PROTOCOL_IPV4)
		{
			configureInterface4(gPdpContList[cid - 1].addresses, gPdpContList[cid - 1].ifname);
		}
		else if(pdptype == PROTOCOL_IPV4V6)		
		{
				char *ptemp = gPdpContList[cid - 1].addresses;
				char ipaddr[INET_ADDRSTRLEN+1] = {0};
				int i = 0;
				
				do{
					ipaddr[i] = *ptemp;
					i++;
					ptemp++;
				}while(*ptemp != ',' && i < INET_ADDRSTRLEN);

				ALOGD("configureInterface4 ip = %s", ipaddr);
				configureInterface4(ipaddr, gPdpContList[cid - 1].ifname);		
		}
#endif
#else
		configureInterface(gPdpContList[cid - 1].ifname,gPdpContList[cid - 1].addresses);
#endif
    }
	
	/*20151109 majun del for useless code*
	*add by 051368 for  PDP set cond start*
	if (1 == cid)
    	{
		property_set(ZTERILD_PDP_CGDCONT, "1"); 
		ALOGI("set cgdcont 1");
		result = 1;
		RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_PDP_SETUP_RESULT,&result, sizeof(result));
	}
	*add by 051368 for  PDP set cond start*/
	
	RIL_onRequestComplete(token, RIL_E_SUCCESS, &(gPdpContList[cid - 1]), sizeof(RIL_Data_Call_Response_v6));
	return;
error:
	resetPdpContextLocalStatus(cid);
	ALOGD("%s failed!", __FUNCTION__);
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	if (1 == cid)
    	{
		result = 0;
		RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_PDP_SETUP_RESULT,&result, sizeof(result));
	}
	property_set("ril.pdpsetup.fail", "0");
	return;
}


//"10.42.215.8.255.0.0.0"
static void setPdpContextLocalStatus(int cid, char *PDP_type, char *address, char *gateway, char *dns1, char *dns2, char *apn)
{
	char strAddr[128]={0};

	pthread_mutex_lock(&g_datacall_info_mutex); 		
	gPdpContList[cid - 1].cid = cid;
	gPdpContList[cid - 1].active = 1;
	gPdpContList[cid - 1].status = PDP_FAIL_NONE;
	gPdpContList[cid - 1].suggestedRetryTime = 0;//0x7fffffff;

	char* pSrcaddr = address;
	char* pdestaddr = &(strAddr[0]);
	int count = 0;
	while(1)
	{
		
		if(*pSrcaddr == '.') count++;
		if(count > 3)
		{
			ALOGD("%s:%c:%d,%s,strlen:%d",__FUNCTION__,*pSrcaddr,count,strAddr,strlen(strAddr)+1);
			break;
		}
		*pdestaddr = *pSrcaddr;
		pSrcaddr++;
		pdestaddr++;
	}
	asprintf(&(gPdpContList[cid - 1].addresses),"%s",strAddr);
	
	if( PDP_type != NULL)
	{
		asprintf(&(gPdpContList[cid - 1].type),"%s",PDP_type);
	}
	else
	{
		char strType[10]={"IP"};
		asprintf(&(gPdpContList[cid - 1].type),"%s",strType);
	}
	getGwFromIP(gPdpContList[cid - 1].addresses,&(gPdpContList[cid - 1].gateways));
	asprintf(&(gPdpContList[cid - 1].dnses), "%s %s", dns1,dns2);
	//20151109 majun mod for ECM netcard 
	asprintf(&(gPdpContList[cid - 1].ifname), "%s%d", DEV_NETCARD, cid-1);
	//asprintf(&(gPdpContList[cid - 1].ifname), "%s%d", DEV_NETCARD, 0); //bufantong 20170105
	if(apn != NULL)
	{
		strncpy(gApnProfile[cid - 1].Apn,apn,128);
	}
	dumpPdpContext(cid);
	pthread_mutex_unlock(&g_datacall_info_mutex);
	//ALOGD("%s:successful", __FUNCTION__);
}

/* 重置PDP上下文当前的状态 */
static void resetPdpContextLocalStatus(int cid)
{
    pthread_mutex_lock(&g_datacall_info_mutex);
	FREE(gPdpContList[cid - 1].addresses);
	FREE(gPdpContList[cid - 1].dnses);
	FREE(gPdpContList[cid - 1].gateways);
	FREE(gPdpContList[cid - 1].ifname);
	FREE(gPdpContList[cid - 1].type);
    memset(&(gPdpContList[cid - 1]), 0x00, sizeof(RIL_Data_Call_Response_v6));    
	memset(&(gApnProfile[cid - 1]), 0x00, sizeof(Apn_Profile));    
	gApnProfile[cid - 1].profile = -1;
    pthread_mutex_unlock(&g_datacall_info_mutex);
	//ALOGD("%s:successful", __FUNCTION__);
}

static int getPdpContextNum(void)
{
	int cid =0;
	int index = 0;

	for(cid=0; cid< MAX_DATA_CALLS; cid++) 
	{
		if(gPdpContList[cid].active ==1)
	{
			index++;
		}
	}
	ALOGD("%s: %d \n", __FUNCTION__,index);
	return index;

}
static int getPdpContextNumForCgev(void)
{
	int cid =0;
	int index = 0;

	for(cid=0; cid< MAX_DATA_CALLS; cid++) 
	{
		if(gPdpContList[cid].active ==1&&gPdpContList[cid].cid ==1)
	{
			index++;
		}
	}
	ALOGD("%s: %d \n", __FUNCTION__,index);
	return index;

}
void ril_unsol_data_call_list_changed(void)
{
	int cid =0;
	int num = getPdpContextNum();
	if(num != 0)
	{
		RIL_Data_Call_Response_v6 *pdpResponses = allocPDPContextList(num);
		syncPdpContextList(pdpResponses,num);
		//ALOGD("%s: cid:%d,ifname:%s,addresses:%s", __FUNCTION__, pdpResponses->cid,pdpResponses->ifname,pdpResponses->addresses);
		RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED, pdpResponses, num * sizeof(RIL_Data_Call_Response_v6));
		freePDPContextList(pdpResponses, num);
	}
	else
	{
		RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED, NULL, 0);		
	}
	
}

static void ril_unsol_pdpContextChanged(void *param)
{
	int* pCid = (int*)param;
	int cid =1;
    ALOGD("%s: cid:%d", __FUNCTION__, *pCid);

	/* 同步PDP上下文 */
	syncPdpContext(cid);
	if (gPdpContList[cid - 1].active == 1)
    {
    		hostonModem();
		setNetProperty(cid,gPdpContList[cid - 1].ifname); /* 设置网络 */
		configureInterface(gPdpContList[cid - 1].ifname,gPdpContList[cid - 1].addresses); /* 配置接口 */
    }
    //ril_unsol_data_call_list_changed();

	return;

}




void handle_zgipdns(const char *s, const char *smsPdu)
{
    char *dns1 = NULL, *dns2 = NULL,*address = NULL, *gateway = NULL,*PDP_type = NULL;
    int  cid_parsed, cid_num;
	
	pthread_mutex_lock(&pdp_act_mutex);
	pdp_act = 1;
	pthread_cond_signal(&pdp_activated);
	pthread_mutex_unlock(&pdp_act_mutex);
	
    int err = parse_zgipdns(s, &cid_num, &cid_parsed, &PDP_type, &address, &gateway, &dns1, &dns2);
	//setPdpContextLocalStatus(cid_parsed, PDP_type, address, gateway, dns1, dns2, NULL);
	FREE(address);
	FREE(gateway);
	FREE(PDP_type);
	FREE(dns1);
	FREE(dns2);
}


void handle_cgev(const char *line, const char *smsPdu)
{
    int cid = 0;
	int action = parse_cgev(line,&cid);  /* 解析cgev */
	
	/*if(action == RIL_PS_NET_NOTICE_ACT)	
	{
		


	}
	else*/ if(action == RIL_PS_NET_NOTICE_DEACT)
	{
		pthread_mutex_lock(&pdp_deact_mutex);
		pdp_deact = 1;
		pthread_cond_signal(&pdp_deactivated);
		pthread_mutex_unlock(&pdp_deact_mutex);
	
	}
	ALOGD("%s: 111111start ; net :%d; action:%d", __FUNCTION__,cid,action);
	//return;
	if (action == RIL_PS_NET_NOTICE_DEACT)
	{
		if (1 == cid)
    		{
			char mode[PROPERTY_VALUE_MAX];
   			property_get(PROPERTY_MODEM_PS_STATE, mode, "1");
   			if(0 == strcmp(mode,"0")){
				property_set(ZTERILD_PDP_CGDCONT, "0"); 
				ALOGI("handle_cgev set cgdcont 0");
   			}
		}
#ifdef SUPPORT_IPV6
		if(gPdpContList[cid - 1].ifname == NULL)
		{
			return;
		}
#endif	
		if(gPdpContList[cid - 1].ifname == NULL || strlen(gPdpContList[cid - 1].ifname) == 0){
			ALOGD("%s: 111111start ; ifname is null,return !!!", __FUNCTION__);	
			/* 未经许可的请求响应 */
			RIL_onUnsolicitedResponse(RIL_UNSOL_DATA_CALL_LIST_CHANGED, NULL, 0);	
			return;
		}
		ALOGD("%s: 111111start ; ifname :%s", __FUNCTION__ ,gPdpContList[cid - 1].ifname);
		/*20151109 majun del for : 禁用启用数据后无法上网*/
		//deconfigureInterface(gPdpContList[cid - 1].ifname);
		ALOGD("%s: 111111start ; cid:%d,ifname :%s", __FUNCTION__ ,gPdpContList[cid - 1].cid,gPdpContList[cid - 1].ifname);
		//resetNetProperty(gPdpContList[cid - 1].cid,gPdpContList[cid - 1].ifname);
		resetPdpContextLocalStatus(cid);
		ril_unsol_data_call_list_changed();
		ALOGD("%s: start deconfigureInterface ; over !!!", __FUNCTION__ );
		
	}
	else if(action == RIL_PS_NET_NOTICE_ACT)
	{
		ALOGD("%s: 111111  ACT ;over", __FUNCTION__ );
		/* 得到工作队列  PDP上下文改变 */
		enque(getWorkQueue(SERVICE_PS), ril_unsol_pdpContextChanged, &(cid));
	}
    return ;
}

//added by zhangzhiliang, 20140401, for defining pdp context for lte start
void ril_request_define_pdp_context(int request, void *data, size_t datalen, RIL_Token token)
{
	ALOGD("%s ignore, this is for lte only", __FUNCTION__);
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return;	
}
//added by zhangzhiliang, 20140401, for defining pdp context for lte end

//added by zhangzhiliang, 20140612, for opening/closing download port, begin
static int setZFlag (const char *dev, const char *flag)
{
    char    cmdString[MAX_AT_LENGTH] = { 0 };
    ATResponse  *p_response = NULL;

	sprintf(cmdString, "AT+ZFLAG=\"%s\",%s", dev, flag);
    int err = at_send_command(cmdString, &p_response);
    if (err < 0 || p_response->success == 0)
    {
        ALOGD("setZFlag failed");
		err =-1;
    }
	else
	{
		ALOGD("setZFlag successfully");
	}
	at_response_free(p_response);
	return err;
}

void ril_request_set_zflag(int request, void *data, size_t datalen, RIL_Token token)
{
	const char	*dev = ((const char **) data)[0];
	const char	*flag = ((const char **) data)[1];
	int err =0;

	ALOGD("%s: dev=%s,flag=%s", __FUNCTION__, dev, flag);

	err = setZFlag(dev, flag);
	DO_ERROR_JDUGE;
	
	RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	return;
error:
	ALOGD("%s failed!", __FUNCTION__);
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	return; 
}
//added by zhangzhiliang, 20140612, for opening/closing download port, end

/*20151109 majun mod for YF*/
void ril_request_setup_data_call(int request, void *data, size_t datalen, RIL_Token token)
{
    const char  *radio_technology = ((const char **) data)[0];
    const char  *profile_type = ((const char    **) data)[1];
    const char  *apn = ((const char **) data)[2];
    const char  *user = ((const char    **) data)[3];
    const char  *passwd = ((const char  **) data)[4];
    const char  *auth_type_str = ((const char   **) data)[5];
    const char  *protocol = ((const char    **) data)[6];
	/*20151109 majun mod for : 原生android RIL.java里面只带7个参数，第8个参数不存在，会造成rild异常退出*/
	const char  *apn_type = "default";
	//const char  *apn_type = ((const char    **) data)[7];//added by zhangzhiliang, 20140304, for getting cid when defining pdp context
    int profile = atoi(profile_type);
    int auth_type = atoi(auth_type_str);

    ALOGD("%s: profile=%d,apn=%s,user=%s,passwd=%s,auth_type=%d,protocol=%s,apn_type=%s", __FUNCTION__, profile, apn, user ? user : "NULL", passwd ? passwd : "NULL", auth_type, protocol, apn_type);

    if (is_ppp_enabled())// For PPP connection, alway use default one
    {
        setupPppConnection(token, profile, apn, auth_type, user, passwd, protocol);
    }
	else
	{
    	setupEthConnection(token, profile, apn, auth_type, user, passwd, protocol, apn_type);
	}

}



void ril_request_deactivate_data_call(int request, void *data, size_t datalen, RIL_Token token)
{
    int cid = atoi(((char   **) data)[0]);
	ALOGD("%s:  cid:=%d", __FUNCTION__, cid);	
//#if 1	
	if( getCurNwMode() ==17 && cid ==1)
	{
		//20151109 majun del for useless LOG
		//ALOGD("%s:  detachLte  start!!!",__FUNCTION__);
		//detachLte();
		//LOGD("%s:  detachLte over!!!!",__FUNCTION__);
		//deactiviatePdp(cid);		
	}
	else
	{
		deactivatePdp(cid);
	}
	
	/*hread_mutex_lock(&pdp_deact_mutex);
	while(pdp_deact !=1)
	{
		pthread_cond_wait(&pdp_deactivated, &pdp_deact_mutex);
	}
	pdp_deact = 0;
	pthread_mutex_unlock(&pdp_deact_mutex);	
     */
//#endif	
	deactivateRndisInterface(cid);
	char	ifname[32] = {0};
	//20151109 majun mod for ECM netcard 
	sprintf(ifname, "%s%d", DEV_NETCARD,cid-1);
       ALOGD("%s:%d;%s", __FUNCTION__, cid,ifname);	
	/*20151109 majun del for : 此行代码会造成网卡异常,禁用再启用数据后无法上网*/
	//deconfigureInterface(ifname);
	//resetNetProperty(cid,ifname);
	resetPdpContextLocalStatus(cid);
	
	/*20151109 majun del for uesless*
	*add by 051368 for  PDP set cond start*
	if (1 == cid)
    	{
		property_set(ZTERILD_PDP_CGDCONT, "0"); 
		ALOGI("set cgdcont 0");
	}
	*add by 051368 for  PDP set cond start*/
	
    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
}


void ril_request_last_data_call_fail_cause(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    int err = at_send_command_singleline("AT+PEER", "+PEER:", &response);
	DO_RESPONSE_ERROR_JDUGE;

    char *line = response->p_intermediates->line;
    err = at_tok_start(&line);
	DO_ERROR_JDUGE;

    int result = 0;
    err = at_tok_nextint(&line, &result);
	DO_ERROR_JDUGE;

    RIL_onRequestComplete(token, RIL_E_SUCCESS, &result, sizeof(result));
    at_response_free(response);
	return;
error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    at_response_free(response);
	return;
}



/*
	直接从本地缓存中获取
	本地缓存在主动建立 PDP 和 网络激活 PDP以及去激活时销毁 
*/
void ril_request_data_call_list(int request, void *data, size_t datalen, RIL_Token token)
{
	int num = getPdpContextNum();
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);	return;
	if(num ==0) 
	{
		RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
		ALOGE("%s: num ==0 \n", __FUNCTION__);
	}
	else
	{
	RIL_Data_Call_Response_v6* pdpResponses = allocPDPContextList(num);
	syncPdpContextList(pdpResponses,num);
    RIL_onRequestComplete(token, RIL_E_SUCCESS, pdpResponses, num * sizeof(RIL_Data_Call_Response_v6));
	freePDPContextList(pdpResponses, num);
	}
	return;
}


void ril_request_fast_dormancy(int request, void *data, size_t datalen, RIL_Token token)
{
}

void ril_request_inform_ims_state(int request, void *data, size_t datalen, RIL_Token token)
{
	ATResponse	*response = NULL;
	int err = -1;
    const char  *state = ((const char **) data)[0];
	int ims_state = atoi(state);
    char    cmdString[MAX_AT_LENGTH] = { 0 };
    sprintf(cmdString, "AT+ZIMSSTATE=%d", ims_state);
	err = at_send_command(cmdString, &response);
	DO_ERROR_JDUGE;

	RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	at_response_free(response);
	return;
error:
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(response);
	return;

}

void ril_request_attach_gprs(int request, void *data, size_t datalen, RIL_Token token)
{    
    ATResponse  *response = NULL;
    int err = at_send_command_timeout("AT+CGATT=1", &response,TIMEOUT_CGATT);
    if (err < 0 || response->success == 0)
    {
        goto error;
    }

	RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	at_response_free(response);
	return;
error:
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(response);
	return;
}

void ril_request_detach_gprs(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    int err  = at_send_command_timeout("AT+CGATT=0", &response,TIMEOUT_CGATT);
    if (err < 0 || response->success == 0)
    {
        goto error;
    }

	RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	at_response_free(response);
	return;
error:
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(response);
	return;

}

void ril_request_query_3gqos(int request, void *data, size_t datalen, RIL_Token token)
{
	ATResponse	*response = NULL;
	int err =  at_send_command_singleline("AT+CGEQREQ?", "+CGEQREQ:", &response);
	DO_RESPONSE_ERROR_JDUGE;
	ATLine	*p_cur;
	int result[4] =  {	 0	  };
	int i = 0;

	for (i = 0, p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next, i++)
	{
		char	*line = p_cur->line;
		err = at_tok_start(&line);
		DO_ERROR_JDUGE;

		err = at_tok_nextint(&line, &(result[0])) ;
		DO_ERROR_JDUGE;

		err = at_tok_nextint(&line, &(result[1])) ;
		DO_ERROR_JDUGE;

		err = at_tok_nextint(&line, &(result[2])) ;
		DO_ERROR_JDUGE;

		err = at_tok_nextint(&line, &(result[3])) ;
		DO_ERROR_JDUGE;
	}
	
	if (1 != result[0]) //cid=1, only for engineer mode
	{
		ALOGD("%s: Other CID Qos,don't return to ap\n", __FUNCTION__);
		goto error;
	}
	RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(result));
	at_response_free(response);
	return ;
error:
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(response);
	return ;
}

void ril_request_set_3gqos(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    int err = -1;
    char    cmdString[MAX_AT_LENGTH];
    char    *traffic_class = ((char **) data)[0];
    char    *ul_bitrate = ((char    **) data)[1];
    char    *dl_bitrate = ((char    **) data)[2];
    snprintf(cmdString, sizeof(cmdString), "AT+CGEQREQ=%s,%s,%s,%s", "1", traffic_class, ul_bitrate, dl_bitrate); //cid=1, only for engineer mode
    err = at_send_command(cmdString, &response);
    if (err < 0 || response->success == 0)
    {
        goto error;
    }
	RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	at_response_free(response);
	return ;
error:
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(response);
	return ;
}	

void ril_request_set_ps_transfer(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    int err = -1;
    char    cmdString[MAX_AT_LENGTH];
    int    transferDir = 0;
    /*add by 051368 for pdp syn start*/
//    synPdpV4V6Context(1);
    /*add by 051368 for pdp syn end*/
    snprintf(cmdString, sizeof(cmdString), "AT+ZMOVEPS=%d", transferDir); 
    err = at_send_command(cmdString, &response);
	DO_MM_RESPONSE_ERROR_JDUGE ;
	set_modem_ps_state(1);
	RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	at_response_free(response);
	return ;
error:
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(response);
	return ;
}	
void set_modem_ps_state(int state)
{
 	if (state == 0)
 	{
		property_set(PROPERTY_MODEM_PS_STATE, "0"); 
		ALOGI("set modem psstate 0");
	}
	else
	{
		property_set(PROPERTY_MODEM_PS_STATE, "1"); 
		ALOGI("set modem psstate 1");
	}
}

//add by 051368 for ps moving start
void ril_request_syn_pdp_context(int request, void *data, size_t datalen, RIL_Token token)
{
    int cid =((int  *) data)[0];
    ALOGD("ril_request_syn_pdp_context CID is %d", cid);
    if(0 == cid)
    {
        RIL_onUnsolicitedResponse(RIL_UNSOL_PDP_SYN_CONTEXT, NULL, 0);	
    }
    else
    {
        synPdpV4V6Context(cid);
    }
	RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
}

void ril_request_get_zps_stat(int request, void *data, size_t datalen, RIL_Token token)
{
	ATResponse  *p_response = NULL;
      int err;
	int zpsstat, tmpvalue;
	char    *line = NULL;
	ALOGD(" ril_request_get_zps_stat");
	err = at_send_command_singleline("AT+ZPSSTAT?", "+ZPSSTAT:", &p_response);
      if (err < 0 || p_response->success == 0 || p_response->p_intermediates == NULL)
      {
          goto error;
      }
	line = p_response->p_intermediates->line;
      err = at_tok_start(&line);
	DO_ERROR_JDUGE;
	err = at_tok_nextint(&line, &tmpvalue);
	DO_ERROR_JDUGE;
	err = at_tok_nextint(&line, &zpsstat);
	DO_ERROR_JDUGE;
      ALOGD("%d: ril_request_get_zps_stat", zpsstat);
	RIL_onRequestComplete(token, RIL_E_SUCCESS, &zpsstat, sizeof(zpsstat));
	at_response_free(p_response);
      return;
error:
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	at_response_free(p_response);
      return ;
}
//add by 051368 for ps moving end


#ifdef ANDROID5_1_AND_6_0_SUPPORTED
/****************************Boris 20170401************************/
void ril_request_allow_data(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

//modified by Boris ,20170401  
	 RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);//The response must be RIL_E_REQUEST_NOT_SUPPORTED 20170318 Boris ???
	 	ALOGD("ril_request_allow_data response must be RIL_E_REQUEST_NOT_SUPPORTED for 5.1");
		
}

/**********chenjun 20170504**********/
void ril_request_start_lce(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen); 
	RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);//The response must be RIL_E_REQUEST_NOT_SUPPORTED 20170318 Boris ???
	ALOGD("ril_request_start_lce response must be RIL_E_REQUEST_NOT_SUPPORTED for 5.1");	
}

void ril_request_stop_lce(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen); 
	RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);//The response must be RIL_E_REQUEST_NOT_SUPPORTED 20170318 Boris ???
	ALOGD("ril_request_stop_lce response must be RIL_E_REQUEST_NOT_SUPPORTED for 5.1");	
}

void ril_request_pull_lcedata(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen); 
	RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);//The response must be RIL_E_REQUEST_NOT_SUPPORTED 20170318 Boris ???
	ALOGD("ril_request_pull_lcedata response must be RIL_E_REQUEST_NOT_SUPPORTED for 5.1");	
}
/**************************************/
#endif	



