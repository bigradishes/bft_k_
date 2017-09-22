/******************************************************************************
 * *(C) Copyright 2012 ZTE International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/

#include <assert.h>
#include <cutils/properties.h>
#include "at_tok.h"
#include "atchannel.h"
#include "zte-ril.h"
#include "misc.h"
#include "ril-requestdatahandler.h"
#include "work-queue.h"
#define MAX_MSG_ON_SIM    60   //128
//mod by zxj 20131121 for TSP 7510 modem and dual standby begin
#define MAX_SIM_PDU_LENGTH 164//177
#define MAX_PDU_LENGTH    400   //128
#define MAX_GSM_BROAD_CAST_SMS_CONFIG_INFO (40 * 40)
#define MAX_GSM_BROAD_CAST_SMS_DATA_CODE_CONFIG_INFO 40
#define INVALID_BROAD_CAST_MESSAGE_IDENTIFY -1UL
#define INVALID_BROAD_CAST_DATA_CODE_SCHEME -1UL
#define MSG_STATUS_ON_ICC_READ 1
#define MSG_STATUS_ON_ICC_UNREAD 0
static int isSimSmFull = -1;
static int simSmFreeCount = -1;
void init_sim_sms_full();

//add by zxj 20131121 for TSP 7510 modem and dual standby end
int String2Bytes(char *pSrc, char *pDst, int nSrcLength)
{
    int i = 0;
    if (pSrc == NULL || pDst == NULL || nSrcLength < 0)
    {
        return -1;
    }

    for (i = 0; i < nSrcLength; i += 2)
    {
        if (*pSrc >= '0' && *pSrc <= '9')// 输出高4位
        {
            *pDst = (*pSrc - '0') << 4;
        }
        else
        {
            *pDst = (*pSrc - 'A' + 10) << 4;
        }
        pSrc++;
        if (*pSrc >= '0' && *pSrc <= '9')// 输出低4位
        {
            *pDst |= *pSrc - '0';
        }
        else
        {
            *pDst |= *pSrc - 'A' + 10;
        }

        pSrc++;
        pDst++;
    }
    return nSrcLength / 2;// 返回目标数据长度
}
void ril_handle_cmd_send_sms_timeout(const char *cmd, const char *smsPdu, const char *prefix, RIL_Token token, long long timeoutMS)
{
    ATResponse  *response = NULL;
    int err;
    RIL_SMS_Response    result;
    char    *line;

    err = at_send_command_sms_timeout(cmd, smsPdu, prefix, &response, timeoutMS);
    if (err < 0 || (response->success == 1 && !response->p_intermediates))
    {
        goto error;
    }
    else if (response->success == 0)
    {
        goto cms_error;
    }
    line = response->p_intermediates->line;
    memset(&result, 0, sizeof(result));
    err = at_tok_start(&line);
    DO_ERROR_JDUGE;

    err = at_tok_nextint(&line, &(result.messageRef));
    DO_ERROR_JDUGE;
    if (at_tok_hasmore(&line))
    {
        err = at_tok_nextstr(&line, &(result.ackPDU));
        DO_ERROR_JDUGE;
    }
    RIL_onRequestComplete(token, RIL_E_SUCCESS, &result, sizeof(result));
    goto exit;
cms_error:
    if (at_get_cms_error(response) == CMS_NW_TIMEOUT)
    {
        RIL_onRequestComplete(token, RIL_E_SMS_SEND_FAIL_RETRY, NULL, 0);
        goto exit;
    }
error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
    at_response_free(response);
}

void ril_request_get_smsc_address(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ATResponse  *response = NULL;
    int result[1];
    char    *line, *addr, *sca;
    int tosca, err;

    err = at_send_command_singleline("AT+CSCA?", "+CSCA:", &response);
	DO_RESPONSE_ERROR_JDUGE;

    line = response->p_intermediates->line;
    err = at_tok_start(&line);
    DO_ERROR_JDUGE;
    err = at_tok_nextstr(&line, &addr);
    DO_ERROR_JDUGE;
    err = at_tok_nextint(&line, &tosca);
    DO_ERROR_JDUGE;
    if (tosca == 145 && addr[0] != '+')
    {
        sca = alloca(sizeof(char) * (strlen(addr) + 1 + 1));
        sca[0] = '+';
        sca[1] = 0;
        strcat(sca, addr);
    }
    else
    {
        sca = addr;
    }
    ALOGI("%s: sca: %s, tosca: %d\n", __FUNCTION__, sca, tosca);
    RIL_onRequestComplete(token, RIL_E_SUCCESS, sca, strlen(sca));
    goto exit;
error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
    at_response_free(response);
}

void setCbconfig(int selected, int fromServiceId, int toServiceId, RIL_GSM_BroadcastSmsConfigInfo *cbConfig)
{
    cbConfig = malloc(sizeof(RIL_GSM_BroadcastSmsConfigInfo));
    assert(cbConfig != NULL);
    cbConfig->selected = !selected;
    cbConfig->fromServiceId = fromServiceId;
    cbConfig->toServiceId = toServiceId;
    cbConfig->fromCodeScheme = cbConfig->toCodeScheme = INVALID_BROAD_CAST_DATA_CODE_SCHEME;
}

void setCbconfigByDcss(RIL_GSM_BroadcastSmsConfigInfo *cbConfig_dcss, int fromServiceId, int toServiceId, RIL_GSM_BroadcastSmsConfigInfo *cbConfig)
{
    cbConfig = malloc(sizeof(RIL_GSM_BroadcastSmsConfigInfo));
    assert(cbConfig != NULL);
    memcpy(cbConfig, cbConfig_dcss, sizeof(*cbConfig));
    cbConfig->fromServiceId = fromServiceId;
    cbConfig->toServiceId = toServiceId;
}

int getCbConfig_mids(void *cbConfig_dcssin,void *cbConfigin, int *p_count, int dcss_count, int selected, char *rangestr)
{
	char *end, *rangefrom;
	int fromServiceId, toServiceId, i, count = *p_count;
	RIL_GSM_BroadcastSmsConfigInfo	*cbConfig[MAX_GSM_BROAD_CAST_SMS_CONFIG_INFO] = {NULL};
	RIL_GSM_BroadcastSmsConfigInfo	*cbConfig_dcss[MAX_GSM_BROAD_CAST_SMS_DATA_CODE_CONFIG_INFO] = {NULL};

    memcpy(cbConfig, cbConfigin, sizeof(cbConfig));
    memcpy(cbConfig_dcss, cbConfig_dcssin, sizeof(cbConfig_dcss));

	if (count + dcss_count + 1 < MAX_GSM_BROAD_CAST_SMS_CONFIG_INFO - 1)
	{
		rangefrom = strsep(&rangestr, "-");
		fromServiceId = strtol(rangefrom, &end, 10);
		if (end == rangefrom)
		{
			return -1;
		}
		if (rangestr != NULL)
		{
			toServiceId = strtol(rangestr, &end, 10);
			if (end == rangestr)
			{
				return -1;
			}
		}
		else
		{
			toServiceId = fromServiceId;
		}
		if (dcss_count <= -1)
		{
			*p_count = *p_count + 1;
			setCbconfig(selected, fromServiceId, toServiceId, cbConfig[++count]);
		}
		else
		{
			for (i = count + 1; i <= count + dcss_count + 1; i++)
			{
				setCbconfigByDcss(cbConfig_dcss[i - count - 1], fromServiceId, toServiceId, cbConfig[i]);
			}
			*p_count +=dcss_count + 1;
		}
	}
	else
	{
		ALOGE("%s: Max limit %d passed, can not send all ranges", __FUNCTION__, MAX_GSM_BROAD_CAST_SMS_CONFIG_INFO);
		return -1;
	}
	return 0;
}

void freeGetBroadcastMem(int count, int dcss_count, void *cbConfig_dcssin, void *cbConfigin, ATResponse *response)
{
    int i;
	RIL_GSM_BroadcastSmsConfigInfo	*cbConfig[MAX_GSM_BROAD_CAST_SMS_CONFIG_INFO] = {NULL};
	RIL_GSM_BroadcastSmsConfigInfo	*cbConfig_dcss[MAX_GSM_BROAD_CAST_SMS_DATA_CODE_CONFIG_INFO] = {NULL};

    memcpy(cbConfig, cbConfigin, sizeof(cbConfig));
    memcpy(cbConfig_dcss, cbConfig_dcssin, sizeof(cbConfig_dcss));
	for (i = 0; i <= dcss_count; i++)
	{
		free(cbConfig_dcss[i]);
	}
	for (i = 0; i <= count; i++)
	{
		free(cbConfig[i]);
	}
	at_response_free(response);
}

int getCbConfig_dcss(void *cbConfig_dcssin, char *dcss, int *p_dcss_count, int selected)
{
	char *end, *rangefrom, *rangestr;
	RIL_GSM_BroadcastSmsConfigInfo	*cbConfig_dcss[MAX_GSM_BROAD_CAST_SMS_DATA_CODE_CONFIG_INFO] = {NULL};
	int dcss_count = *p_dcss_count;

    memcpy(cbConfig_dcss, cbConfig_dcssin, sizeof(cbConfig_dcss));
	while (at_tok_nextstr(&dcss, &rangestr) == 0)
	{
		if (dcss_count >= MAX_GSM_BROAD_CAST_SMS_DATA_CODE_CONFIG_INFO - 1)
		{
			ALOGE("%s: Max limit %d passed, can not send all ranges", __FUNCTION__, MAX_GSM_BROAD_CAST_SMS_DATA_CODE_CONFIG_INFO);
			return -1;
		}
		*p_dcss_count = *p_dcss_count +1;
		cbConfig_dcss[++dcss_count] = malloc(sizeof(RIL_GSM_BroadcastSmsConfigInfo));
		assert(cbConfig_dcss[dcss_count] != NULL);
		cbConfig_dcss[dcss_count]->selected = !selected;
		rangefrom = strsep(&rangestr, "-");
		cbConfig_dcss[dcss_count]->fromCodeScheme = strtol(rangefrom, &end, 10);
		if (end == rangefrom)
		{
			return -1;
		}
		if (rangestr != NULL)
		{
			cbConfig_dcss[dcss_count]->toCodeScheme = strtol(rangestr, &end, 10);
			if (end == rangestr)
			{
				return -1;
			}
		}
		else
		{
			cbConfig_dcss[dcss_count]->toCodeScheme = cbConfig_dcss[dcss_count]->fromCodeScheme;
		}
		cbConfig_dcss[dcss_count]->fromServiceId = cbConfig_dcss[dcss_count]->toServiceId = INVALID_BROAD_CAST_MESSAGE_IDENTIFY;
		continue;
	}
	return 0;
}

/* Process AT reply of RIL_REQUEST_GSM_GET_BROADCAST_SMS_CONFIG */
void ril_request_gsm_get_broadcast_sms_config(int request, void *data, size_t datalen, RIL_Token token)
{
	UNUSED(request);
	UNUSED(data);
	UNUSED(datalen);
	ATResponse	*response = NULL;
	RIL_GSM_BroadcastSmsConfigInfo	*cbConfig[MAX_GSM_BROAD_CAST_SMS_CONFIG_INFO] = {NULL};
	RIL_GSM_BroadcastSmsConfigInfo	*cbConfig_dcss[MAX_GSM_BROAD_CAST_SMS_DATA_CODE_CONFIG_INFO] = {NULL};
	char	*line, *rangestr, *mids, *dcss;
	int err, selected, count = -1, i, dcss_count = -1;
	err = at_send_command_singleline("AT+CSCB?", "+CSCB:", &response);
	DO_RESPONSE_ERROR_JDUGE;
	line = response->p_intermediates->line;
	err = at_tok_start(&line);
	DO_ERROR_JDUGE;
	err = at_tok_nextint(&line, &selected);
	DO_ERROR_JDUGE;
	err = at_tok_nextstr(&line, &mids);
	DO_ERROR_JDUGE;
	err = at_tok_nextstr(&line, &dcss);
	DO_ERROR_JDUGE;
	skipWhiteSpace(&dcss);
	if (*dcss == '\0')
	{
		dcss = NULL;
	}
	err = getCbConfig_dcss((void*)cbConfig_dcss, dcss, &dcss_count, selected);
	DO_ERROR_JDUGE;

	skipWhiteSpace(&mids);
	if (*mids == '\0')
	{
		for (i = 0; i <= dcss_count; i++)
		{
			cbConfig[i] = cbConfig_dcss[i];
		}
		count = dcss_count;
		dcss_count = -1;
		mids = NULL;
	}
	while (at_tok_nextstr(&mids, &rangestr) == 0)
	{
		err = getCbConfig_mids((void*)cbConfig_dcss,(void*)cbConfig, &count, dcss_count, selected, rangestr);
		DO_ERROR_JDUGE;
	}
	RIL_onRequestComplete(token, RIL_E_SUCCESS, &cbConfig, (count + 1) * sizeof(RIL_GSM_BroadcastSmsConfigInfo *));
	goto exit;

error : ALOGE("%s: Error parameter in response: %s", __FUNCTION__, response->p_intermediates->line);
	RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
	freeGetBroadcastMem(count, dcss_count, cbConfig_dcss, cbConfig, response);
}
void dealCscbCmd(char *mids0, char *mids1, char *dcss0, char *dcss1, RIL_Token token){
	int valid = 0;
	char cmdString[MAX_AT_LENGTH];

    if (mids0 != NULL || dcss0 != NULL)
    {
        if (mids0 != NULL && mids0[strlen(mids0) - 1] == ',')
        {
            mids0[strlen(mids0) - 1] = '\0';
        }
        if (dcss0 != NULL && dcss0[strlen(dcss0) - 1] == ',')
        {
            dcss0[strlen(dcss0) - 1] = '\0';
        }
        /*selected 0 means message types specified in <fromServiceId, toServiceId>
            and <fromCodeScheme, toCodeScheme>are not accepted*/
        sprintf(cmdString, "AT+CSCB=%d,\"%s\",\"%s\"", 1, (mids0 == NULL ? "" : mids0), (dcss0 == NULL ? "" : dcss0));
        valid = 1;
    }
    if (mids1 != NULL || dcss1 != NULL)
    {
        if (mids1 != NULL && mids1[strlen(mids1) - 1] == ',')
        {
            mids1[strlen(mids1) - 1] = '\0';
        }
        if (dcss1 != NULL && dcss1[strlen(dcss1) - 1] == ',')
        {
            dcss1[strlen(dcss1) - 1] = '\0';
        }
        /*selected 1 means message types specified in <fromServiceId, toServiceId>
            and <fromCodeScheme, toCodeScheme>are accepted.*/
        if (valid)
        {
            sprintf(cmdString, "%s;+CSCB=%d,\"%s\",\"%s\"", cmdString, 0, (mids1 == NULL ? "" : mids1),
                    (dcss1 == NULL ? "" : dcss1));
        }
        else
        {
            sprintf(cmdString, "AT+CSCB=%d,\"%s\",\"%s\"", 0, (mids1 == NULL ? "" : mids1), (dcss1 == NULL ? "" : dcss1));
        }
        valid = 1;
    }
	if (!valid)
	{
		RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
		return;
	}
	ril_handle_cmd_default_response(cmdString, token);
}

void freeSetBroadcastMem(char *mids0, char *mids1, char *dcss0, char *dcss1)
{
    if (mids0 != NULL)
    {
        free(mids0);
    }
    if (mids1 != NULL)
    {
        free(mids1);
    }
    if (dcss0 != NULL)
    {
        free(dcss0);
    }
    if (dcss1 != NULL)
    {
        free(dcss1);
    }
}
void getMidsSetBroadcast(RIL_GSM_BroadcastSmsConfigInfo *configInfo, char *mid, int i, int count)
{
	char	*tmp, tmpBuffer[15];

	if (configInfo->fromServiceId == configInfo->toServiceId)
	{
		sprintf(tmpBuffer, "%d,", configInfo->fromServiceId);
	}
	else
	{
		sprintf(tmpBuffer, "%d-%d,", configInfo->fromServiceId, configInfo->toServiceId);
	}
	if (mid == NULL || strstr(mid, tmpBuffer) == NULL)
	{
		tmpBuffer[strlen(tmpBuffer) - 1] = '\0';
		tmp = mid;
		asprintf(&mid, "%s%s%s", (tmp ? tmp : ""), tmpBuffer, (i == (count - 1) ? "" : ","));
		FREE(tmp);
	}			 
}

void ril_request_gsm_set_broadcast_sms_config(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);

    RIL_GSM_BroadcastSmsConfigInfo  **CbConfig = (RIL_GSM_BroadcastSmsConfigInfo    **) data;
    int count = datalen / sizeof(RIL_GSM_BroadcastSmsConfigInfo *), i;
    RIL_GSM_BroadcastSmsConfigInfo  *configInfo = NULL;
    char    *tmp, tmpBuffer[15], *mids[2] = {NULL, NULL}, *dcss[2] = {NULL, NULL};
    for (i = 0; i < count; i++)
    {
        configInfo = CbConfig[i];
        if (configInfo->selected != 0 && configInfo->selected != 1)
        {
            goto error;
        }
        if (((configInfo->fromServiceId >= 0x0) &&
              (configInfo->fromServiceId <= 0xFFFF) &&
              (configInfo->toServiceId <= 0xFFFF) &&
              (configInfo->fromServiceId <= configInfo->toServiceId)))
        {
            getMidsSetBroadcast(configInfo, mids[configInfo->selected], i, count);
        }
        if (!((configInfo->fromCodeScheme >= 0x0) &&
              (configInfo->fromCodeScheme <= 0xFF) &&
              (configInfo->toCodeScheme <= 0xFF) &&
              (configInfo->fromCodeScheme <= configInfo->toCodeScheme)))
        {
            continue;
        }
        if (configInfo->fromCodeScheme == configInfo->toCodeScheme)
        {
            sprintf(tmpBuffer, "%d,", configInfo->fromCodeScheme);
        }
        else
        {
            sprintf(tmpBuffer, "%d-%d,", configInfo->fromCodeScheme, configInfo->toCodeScheme);
        }
        if (dcss[configInfo->selected] == NULL || strstr(dcss[configInfo->selected], tmpBuffer) == NULL)
        {
            tmpBuffer[strlen(tmpBuffer) - 1] = '\0';
            tmp = dcss[configInfo->selected];
            asprintf(&dcss[configInfo->selected], "%s%s%s", (tmp ? tmp : ""), tmpBuffer, (i == (count - 1) ? "" : ","));
            free(tmp);
        }
    }
	dealCscbCmd(mids[0], mids[1], dcss[0], dcss[1], token);
    goto exit;
error : 
    ALOGE("%s: parameter error in RIL_REQUEST_GSM_SET_BROADCAST_SMS_CONFIG", __FUNCTION__);
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
	freeSetBroadcastMem(mids[0], mids[1], dcss[0], dcss[1]);
}

void ril_request_gsm_sms_broadcast_activation(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);
    ATResponse  *response = NULL;
    int mode, mt, bm, ds, bfr, skip, err;
    int activation = *((int *) data);
    char    cmdString[MAX_AT_LENGTH], *line;
    err = at_send_command_singleline("AT+CNMI?", "+CNMI:", &response);
	DO_RESPONSE_ERROR_JDUGE;

    line = response->p_intermediates->line;
    err = at_tok_start(&line);
    DO_ERROR_JDUGE;

    err = at_tok_nextint(&line, &mode);
    DO_ERROR_JDUGE;

    err = at_tok_nextint(&line, &mt);
    DO_ERROR_JDUGE;

    err = at_tok_nextint(&line, &skip);
    DO_ERROR_JDUGE;

    err = at_tok_nextint(&line, &ds);
    DO_ERROR_JDUGE;

    err = at_tok_nextint(&line, &bfr);
    DO_ERROR_JDUGE;

	bm = (activation == 0)?2:0;
    sprintf(cmdString, "AT+CNMI=%d,%d,%d,%d,%d", mode, mt, bm, ds, bfr);
    ril_handle_cmd_default_response(cmdString, token);
    goto exit;
error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
    at_response_free(response);
}

static int get_send_multisms_flag()
{
   char mode[PROPERTY_VALUE_MAX] = {0};
   property_get("ril.sendMultipartmsg", mode, "0");
   if(0 == strcmp(mode,"1"))
   {   
       property_set("ril.sendMultipartmsg", "0");
	   return 1; 
   }
   else 
   {
   	   return 0;
   }
}

void ril_request_send_sms(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);
    char    cmdString[MAX_AT_LENGTH];
    char    smsPdu[MAX_PDU_LENGTH];
    const char  *smsc, *tpdu;
    int tpLayerLength;
    char    temp[20];
    int err;

    /*xichunyan 2012-07-10 set sending AT+CMMS=1 to initializeCallback_msg,here only send AT+CMGS  */
    if(1 == get_send_multisms_flag())
    {
	    err = at_send_command("AT+CMMS=1", NULL);
    }
    smsc = ((const char * *) data)[0];
    tpdu = ((const char * *) data)[1];
    tpLayerLength = strlen(tpdu) / 2;
    if (smsc == NULL)
    {
        smsc = "00";
    }

    sprintf(temp, "AT+CMGS=%d", tpLayerLength);
    strcpy(cmdString, temp);
    sprintf(smsPdu, "%s%s", smsc, tpdu);
    ril_handle_cmd_send_sms_timeout(cmdString, smsPdu, "+CMGS:", token, TIMEOUT_CMGS);
}

void ril_request_sms_acknowledge(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    char    cmdString[MAX_AT_LENGTH];
    int ackSuccess;
    ackSuccess = ((int *) data)[0];

    if (ackSuccess == 1)
    {
        strcpy(cmdString, "AT+CNMA=1");
    }
    else
    {
        strcpy(cmdString, "AT+CNMA=2");
    }
    ril_handle_cmd_default_response(cmdString, token);

    /*** Note:  the screen keeps off when the new sms report, 
           so set the Modem to standby after sending the sms ack, ***/
    setCPstandby(); 
}

void ril_handle_cmd_sms_cmgw_timeout(const char *cmd, const char *sms_pdu, const char *prefix, RIL_Token token, long long msec)
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
	
	init_sim_sms_full();
	simSmFreeCount--;
	if(simSmFreeCount == 0)
	{
	    RIL_onUnsolicitedResponse(RIL_UNSOL_SIM_SMS_STORAGE_FULL, NULL, 0);
		isSimSmFull = 1;
	}
    goto exit;
    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}

//mod by zxj 20131121 for TSP 7510 modem and dual standby begin
void ril_request_write_sms_to_sim(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);
    char    cmdString[MAX_AT_LENGTH];
    char    smsPdu[MAX_PDU_LENGTH];
    RIL_SMS_WriteArgs   *p_args;
    const char  *smsc;
    int length, err, smscLength = 0;
    p_args = (RIL_SMS_WriteArgs *) data;
    length = strlen(p_args->pdu) / 2;
    smsc = p_args->smsc;
    if((p_args->status != MSG_STATUS_ON_ICC_READ)&&(p_args->status != MSG_STATUS_ON_ICC_UNREAD))
    {
        sprintf(cmdString, "AT+CMGW=%d,%d", length, p_args->status);
        if (smsc == NULL)
        {
            smsc = "00";
        }
        sprintf(smsPdu, "%s%s", smsc, p_args->pdu);
    }
	else
    {
    	String2Bytes(p_args->pdu,(char *)&smscLength, 2);
    	length = length - smscLength - 1;
    	if(length>MAX_SIM_PDU_LENGTH)
        {
            char pduchecked[MAX_PDU_LENGTH] = {0};
            memcpy(pduchecked, p_args->pdu, (MAX_SIM_PDU_LENGTH + smscLength+1)*2);
    	    sprintf(smsPdu, "%s",  (char*)&pduchecked);
        	length = MAX_SIM_PDU_LENGTH;
        }
    	else
        {
			sprintf(smsPdu, "%s",  p_args->pdu);
	    }
        sprintf(cmdString, "AT+CMGW=%d,%d", length, p_args->status);
    }
    ril_handle_cmd_sms_cmgw_timeout(cmdString, smsPdu, "+CMGW:", token, TIMEOUT_CMGW);
}
//mod by zxj 20131121 for TSP 7510 modem and dual standby end

void ril_request_delete_sms_on_sim(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);
    ATResponse  *response = NULL;
    char    cmdString[MAX_AT_LENGTH];
	int err = 0;
	
    sprintf(cmdString, "AT+CMGD=%d", ((int *) data)[0]);
    err = at_send_command_timeout(cmdString, &response, TIMEOUT_CMGD);
    if (err < 0 || response->success == 0)
    {
        goto error;
    }

    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	init_sim_sms_full();
	if(simSmFreeCount == 0)
	{
	    RIL_onUnsolicitedResponse(RIL_UNSOL_SIM_SMS_STORAGE_AVAILABLE, NULL, 0);
	}
	simSmFreeCount++;
	isSimSmFull = 0;
	
    goto exit;
    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);

}

void ril_request_report_sms_memory_status(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);
    char    cmdString[MAX_AT_LENGTH];
    int memAvail = ((int    *) data)[0];

    /*xichunyan 2012-07-10 change CMEMFULL to ZMENA  */
    sprintf(cmdString, "AT+ZMENA=%d", !memAvail);
    ril_handle_cmd_default_response(cmdString, token);
}

void ril_request_set_smsc_address(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    char    cmdString[MAX_AT_LENGTH];
    char    *sca = (char    *) data;
    int tosca;

    if (sca[0] == '+')
    {
        tosca = 145;
    }
    else
    {
        tosca = 129;
    }

    sprintf(cmdString, "AT+CSCA=%s", sca);
    ril_handle_cmd_default_response(cmdString, token);
}

void ril_request_acknowledge_incoming_gsm_sms_with_pdu(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    ALOGW("unsupported request: %d\n", request);
    RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
}
//mod by zouxiaojie 2013125 for TSP 7510 modem and dual standby begin
#ifdef ZTE_RIL_EXTEND
void ril_request_sim_sms_capability_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    char    *line = NULL;
    char    *storageLoc = NULL;
    int smsnum[2] = {0, 0};

    int err = at_send_command_singleline("AT+CPMS?", "+CPMS:", &response);
	DO_RESPONSE_ERROR_JDUGE;

    line = response->p_intermediates->line;
    err = at_tok_start(&line);
    DO_ERROR_JDUGE;

    err = at_tok_nextstr(&line, &storageLoc);
    DO_ERROR_JDUGE;

    if (0 != strncmp(storageLoc, "SM", 2))
    {
        goto error;
    }
    err = at_tok_nextint(&line, &(smsnum[0]));//used  nums
    DO_ERROR_JDUGE;

    err = at_tok_nextint(&line, &(smsnum[1]));//total nums
    DO_ERROR_JDUGE;

    RIL_onRequestComplete(token, RIL_E_SUCCESS, smsnum, sizeof(smsnum));
    goto exit;

error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
    at_response_free(response);
}
#endif /* ZTE_RIL_EXTEND */
//mod by zouxiaojie 2013125 for TSP 7510 modem and dual standby end

void handle_cmt(const char *s, const char *smsPdu)
{
    UNUSED(s);
    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_NEW_SMS, smsPdu, strlen(smsPdu));
}

void handle_cmti(const char *s, const char *smsPdu)
{
    UNUSED(smsPdu);

    char *line = NULL, *response;
    int err;
    char    *linesave = NULL;
    int index;
    line = strdup(s);
    linesave = line;
    err = at_tok_start(&line);
    DO_ERROR_JDUGE;

    err = at_tok_nextstr(&line, &response);
    DO_ERROR_JDUGE;

    err = at_tok_nextint(&line, &index);
    DO_ERROR_JDUGE;

    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_NEW_SMS_ON_SIM, &index, sizeof(index));

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

void handle_cds(const char *s, const char *smsPdu)
{
    UNUSED(s);

    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_NEW_SMS_STATUS_REPORT, smsPdu, strlen(smsPdu));
}

void handle_cbm(const char *s, const char *smsPdu)
{
    UNUSED(s);
    UNUSED(smsPdu);

    char *line = NULL, *response;
    int err;
    char    *linesave = NULL;
    int len = strlen(smsPdu);
    char    *cbmPdu = (char *) alloca(len / 2);
    ALOGD("%s CBM: pdu = %s, len = %d", __FUNCTION__, s, len);
    if (len < 88 * 2)
    {
        ALOGD("%s: Error cbm length is not correct", __FUNCTION__);
        goto error;
    }
    else
    {
        convertHexStringToByte(smsPdu, len, cbmPdu);
    }    

    RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_NEW_BROADCAST_SMS, cbmPdu, len / 2);       

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

/* ZTE extended AT cmd +ZMGSF  to indicate memory of SIM is full  */
void handle_mmsg(const char *s, const char *smsPdu)
{
    UNUSED(smsPdu);
    char    *line = NULL;
    int err;
    char    *linesave = NULL;

    /*xichunyan 2012-07-12 change +MMSG to +ZMGSF begin */
    int reason = 0;
    line = strdup(s);
    linesave = line;
    err = at_tok_start(&line);
    DO_ERROR_JDUGE;

    err = at_tok_nextint(&line, &reason);
    DO_ERROR_JDUGE;

    if (reason == 211 || reason == 322)
    {
        RIL_onUnsolicitedResponse(RIL_UNSOL_SIM_SMS_STORAGE_FULL, NULL, 0);
    }
    /*xichunyan 2012-07-12 change +MMSG to +ZMGSF end */
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

static void is_sim_sms_full()
{
    ATResponse  *response = NULL;
    char    *line = NULL;
    char    *storageLoc = NULL;
    int smsnum[2] = {0, 0};
    int isSimSmFullold = isSimSmFull;
	
    int err = at_send_command_singleline("AT+CPMS?", "+CPMS:", &response);
	DO_RESPONSE_ERROR_JDUGE;

    line = response->p_intermediates->line;
    err = at_tok_start(&line);
    DO_ERROR_JDUGE;

    err = at_tok_nextstr(&line, &storageLoc);
    DO_ERROR_JDUGE;

    if (0 != strncmp(storageLoc, "SM", 2))
    {
        goto error;
    }
    err = at_tok_nextint(&line, &(smsnum[0]));//used  nums
    DO_ERROR_JDUGE;

    err = at_tok_nextint(&line, &(smsnum[1]));//total nums
    DO_ERROR_JDUGE;
	if(smsnum[1] != 0)
	{
		if(smsnum[0] == smsnum[1])
		{
		    isSimSmFull = 1;
			simSmFreeCount = 0;
			if(isSimSmFullold != isSimSmFull)
			{
				RIL_onUnsolicitedResponse(RIL_UNSOL_SIM_SMS_STORAGE_FULL, NULL, 0);
			}
		}
		else
		{
		    isSimSmFull = 0;
			simSmFreeCount = smsnum[1] - smsnum[0];
			if(isSimSmFullold != isSimSmFull)
			{
				RIL_onUnsolicitedResponse(RIL_UNSOL_SIM_SMS_STORAGE_AVAILABLE, NULL, 0);
			}
		}
	}
error:
	at_response_free(response);
}

void init_sim_sms_full()
{
    ALOGW("%s: isSimSmFull = %d", __FUNCTION__, isSimSmFull);
	if(isSimSmFull == -1)
	{
		is_sim_sms_full();
	}
}


