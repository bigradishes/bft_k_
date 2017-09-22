/******************************************************************************
 * *(C) Copyright 2012 ZTE International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/

#include "at_tok.h"
#include "atchannel.h"
#include "zte-ril.h"
#include "misc.h"
#include "ril-requestdatahandler.h"
#include "work-queue.h"

#define DO_LINE_JDUGE \
    do{\
        if (!at_tok_hasmore(&line)) continue;\
    }while(0)

int hasZpbicPbOkRecvd = 0;
/* Merge leadcore-android-4.0.1_r1 about ussd ,by gaofeng 02-20 begin */
//<UH, 2011-1-4, lifangdong, Add for USSD ./>
#define USSD_STRING_LENGH_MAX                   182
		/*================USSD mode================*/
#define USSD_DISABLE_UNSOLICITED_RESULT_CODE    0
#define USSD_ENABLE_UNSOLICITED_RESULT_CODE     1
#define USSD_CANCEL_SESSION                        2
		/*================USSD DCS==================*/
#define USSD_DCS_7_BIT                       0x0F  /*7bit default,no message class*/
#define USSD_DCS_8_BIT                       0x44  /* uncompressed, no message class,character set is 8bit*/
#define USSD_DCS_16_BIT                      0x48  /* uncompressed, no message class,character set is UCS2(16bit)  */
#define USSD_DCS_UNKNOWN                     0xff  /* unknown */
/* Merge leadcore-android-4.0.1_r1 about ussd ,by gaofeng 02-20 end */

extern void init_sim_sms_full();

void requestQueryFacilityLockFDsync(void *data, size_t datalen, RIL_Token token)
{
    char    cmdString[MAX_AT_LENGTH];
    ATResponse  *response = NULL;
    ATLine  *p_cur;
    int err = -1;
    char    *facility = ((char  **) data)[0];
    char    *passwd = ((char    **) data)[1];
    char * class = ((char   **) data)[2];
    char    *aid = ((char   **) data)[3];
    int resp = 0,line_num = 0,status_resp = 0;
    char    *line = 0;

    sprintf(cmdString, "AT+CLCK=\"%s\",%d", facility, 2);
    err = at_send_command_singleline_timeout(cmdString, "+CLCK:", &response, TIMEOUT_CLCK);
    DO_RESPONSE_ERROR_JDUGE;
    line = response->p_intermediates->line;
    err = at_tok_start(&line);
    DO_ERROR_JDUGE;
    err = at_tok_nextint(&line, &status_resp);
	DO_ERROR_JDUGE;

    at_response_free(response);
    response = NULL;
    /*if (1 == status_resp)
    {
        sprintf(cmdString, "AT^DNUMCHECK=%d,%d", 0, 1);
        err = at_send_command_singleline_timeout(cmdString, NULL, &response, TIMEOUT_CLCK);
        if (err < 0 || response->success == 0)
        {
            goto error;
        } 
        at_response_free(response);
        response = NULL;
    }*/
    RIL_onRequestComplete(token, RIL_E_SUCCESS, &status_resp, sizeof(status_resp));
    return;
error:    
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);//MYLOG("APP-<-RIL-:GENERIC_FAILURE:RIL_REQUEST_QUERY_FACILITY_LOCK");
    at_response_free(response);
    return;
}

/* Process AT reply of RIL_REQUEST_QUERY_FACILITY_LOCK */
void ril_request_query_facility_lock(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);
    ATResponse  *response = NULL;
    int err, status, classNo, i, lockclass = atoi(((char **) data)[2]), result[1]={0};
    ATLine  *p_cur;
    char    cmdString[MAX_AT_LENGTH]={0}, *code = ((char  **) data)[0], *password = ((char  **) data)[1];
    if (strcmp(code, "FD") == 0)
    {
        requestQueryFacilityLockFDsync(data, datalen, token);
        return;
    }
    if (lockclass == 0)
    {
        sprintf(cmdString, "AT+CLCK=\"%s\",2,\"%s\",7", code, password);
    }
    else
    {
        sprintf(cmdString, "AT+CLCK=\"%s\",2,\"%s\",%d", code, password, lockclass);
    }
    err = at_send_command_multiline_timeout(cmdString, "+CLCK:", &response, TIMEOUT_CLCK);
    DO_MM_RESPONSE_ERROR_JDUGE;
    for (i = 0, p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next, i++)
    {
        char    *line = p_cur->line;
        err = at_tok_start(&line);
        DO_ERROR_JDUGE;
        err = at_tok_nextint(&line, &status);
        DO_ERROR_JDUGE;
        if (!at_tok_hasmore(&line))
        {
            if (status == 1)
            {
                result[0] += 7;
            }
            continue;
        }
        err = at_tok_nextint(&line, &classNo);
        DO_ERROR_JDUGE;
        if (status == 1)
        {
            result[0] += classNo;
        }
    }
    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(result[0]));
    goto exit;
    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}


/* Process AT reply of RIL_REQUEST_GET_CLIR */
void ril_request_get_clir(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ATResponse  *response = NULL;
    int result[2];
    char    *line;
    int err;

    err = at_send_command_singleline_timeout("AT+CLIR?", "+CLIR:", &response, TIMEOUT_CLIR_GET);
    if (err < 0 || response->success == 0)
    {
        goto error;
    }

    line = response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextint(&line, &(result[0]));
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextint(&line, &(result[1]));
    if (err < 0)
    {
        goto error;
    }

    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(result));
    goto exit;

    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}

/* Process AT reply of RIL_REQUEST_QUERY_CALL_FORWARD_STATUS */
void ril_request_query_call_forward_status(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);
    ATResponse  *response = NULL;
    ATLine  *p_cur;
    RIL_CallForwardInfo **result;
    int err,number = 0,i = 0;
	char    cmdString[MAX_AT_LENGTH];
    RIL_CallForwardInfo *info = (RIL_CallForwardInfo    *) data;    
    int class = info->serviceClass;
	int reason = info->reason;
    if (class == 0)
    {
        sprintf(cmdString, "AT+CCFC=%d,%d", info->reason, info->status);
    }
    else
    {
        sprintf(cmdString, "AT+CCFC=%d,%d,,,%d", info->reason, info->status, info->serviceClass);
    }
    err = at_send_command_multiline_timeout(cmdString, "+CCFC:", &response, TIMEOUT_CCFC);
    DO_RESPONSE_JDUGE;
    for (p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next)
    {
        number++;
    }   
    ALOGD("%s: lllllllllll %d\n", __FUNCTION__, number);	
	handelresult(response,number,reason,token);
error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
}
	
void handelresult( ATResponse *response,int  number,int  reason, RIL_Token token)
{   
   char    *ignore;
   int ignoreSubtype,err,i=0; 
   ATLine  *p_cur;
   RIL_CallForwardInfo **result;

   result = alloca(number * sizeof(RIL_CallForwardInfo *));
   
    while (i < number)
    {
        result[i] = alloca(sizeof(RIL_CallForwardInfo));
        memset(result[i], 0, sizeof(RIL_CallForwardInfo));
        i++;
    }
	i=0;
   for (p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next, ++i)
    {
        char    *line = p_cur->line; 
        result[i]->status = 0;
        result[i]->reason = reason;
        result[i]->serviceClass = 0;
        result[i]->toa = 0;
        result[i]->number = NULL;
        result[i]->timeSeconds = 0;
        err = at_tok_start(&line);
        DO_RESPONSE_JDUGE;
        err = at_tok_nextint(&line, &(result[i]->status));
        DO_RESPONSE_JDUGE;
		err = at_tok_nextint(&line, &(result[i]->serviceClass));
        DO_RESPONSE_JDUGE;
        DO_LINE_JDUGE;
        err = at_tok_nextstr(&line, &(result[i]->number));
        DO_RESPONSE_JDUGE;
        DO_LINE_JDUGE;
        err = at_tok_nextint(&line, &(result[i]->toa));
        DO_RESPONSE_JDUGE;
        DO_LINE_JDUGE;
        err = at_tok_nextstr(&line, &ignore);
        err = at_tok_nextint(&line, &ignoreSubtype);
        DO_LINE_JDUGE;
        err = at_tok_nextint(&line, &(result[i]->timeSeconds));
        DO_RESPONSE_JDUGE;

    }
    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, number * sizeof(RIL_CallForwardInfo *));
    at_response_free(response);
error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
}

/* Process AT reply of RIL_REQUEST_QUERY_CALL_WAITING */
void ril_request_query_call_waiting(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    ATResponse  *response = NULL;
    ATLine  *p_cur;
    int err,enable,i;
    int result[2];
    int class = ((int   *) data)[0];
    char    cmdString[MAX_AT_LENGTH];
    if (class == 0) class = 7;
    sprintf(cmdString, "AT+CCWA=1,2,%d", class);
    err = at_send_command_multiline_timeout(cmdString, "+CCWA:", &response, TIMEOUT_CCWA);
    DO_RESPONSE_JDUGE;
    result[1] = 0;
    for (i = 0, p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next, i++)
    {
        char    *line = p_cur->line;
        err = at_tok_start(&line);
       DO_ERROR_JDUGE;
        err = at_tok_nextint(&line, &enable);
        DO_ERROR_JDUGE;
        err = at_tok_nextint(&line, &class);
        DO_ERROR_JDUGE;
        if (enable == 1)
        {
            result[1] +=class;
        } 
    }
    if (result[1] > 0)
    {
        result[0] = 1;
    }
    else
    {
        result[0] = 0;
    }
    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(result));
    goto exit;
error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
exit:
    at_response_free(response);
}

/* Process AT reply of
    RIL_REQUEST_QUERY_CLIP
    RIL_REQUEST_QUERY_COLP
    RIL_REQUEST_QUERY_COLR
    RIL_REQUEST_GET_CNAP
*/
static void ril_request_query_cxxx(const char *cmd, const char *prefix, RIL_Token token)
{
    ATResponse  *response = NULL;
    int err;
    int result;
    int ignore;

    err = at_send_command_singleline(cmd, prefix, &response);
    if (err < 0 || response->success == 0)
    {
        goto error;
    }

    char    *line = response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextint(&line, &ignore);
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

/* Merge leadcore-android-4.0.1_r1 about ussd ,by gaofeng 02-20 begin */
//<UH, 2011-1-4, lifangdong, Add for USSD ./>
int ascToHex(const char *in_str, unsigned short int in_len, char *out_fptr)
{
    signed long int i = 0;
    
    /* Check the validity of parameter */
    if ((NULL==in_str) || (NULL==out_fptr))
    {
        return -1;
    }

    /* ASCII string to HEX string */
    for (; i < in_len; i++)
    {
        sprintf(out_fptr+strlen(out_fptr), "%02X", in_str[i]);
    }

    return 0;
}

void ril_request_send_ussd(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    
    RIL_Ussd_Info *ussd_info_ptr = (RIL_Ussd_Info *)(data);
	char            *cmd = NULL;
    int             err;
    char            hex_buf[USSD_STRING_LENGH_MAX*2+1] = {0};
    char            asc_buf[USSD_STRING_LENGH_MAX*2+1] = {0};
    ATResponse      *p_response = NULL;
    int             mode = ussd_info_ptr->mode ;
    int             dcs = ussd_info_ptr->dcs ;
    char            *ussdStr = ussd_info_ptr->ussdStr ;
    unsigned long   input_len = 0;
   /*
    AT+CUSD=[<n>[,<str>[,<dcs>]]]
    Parameter:
    <n>
    0 disable the result code presentation in the TA
    1 enable the result code presentation in the TA
    2 ccancel session (not applicable to read command response)
    <str>
    String type USSD-string (when <str> parameter is not given, network is not interrogated).
    <dcs>
    3GPP TS 23.038 [25] Cell Broadcast Data Coding Scheme in integer format (default 15)
    15 7bit
    68 8bit
    72 16bit
    */
    if ((mode != USSD_DISABLE_UNSOLICITED_RESULT_CODE &&
         mode != USSD_ENABLE_UNSOLICITED_RESULT_CODE &&
         mode != USSD_CANCEL_SESSION) ||
         (dcs != USSD_DCS_7_BIT &&
          dcs != USSD_DCS_8_BIT &&
          dcs != USSD_DCS_16_BIT) ||
          ussdStr == NULL)
    {
        ALOGE("Invalid parameter mode = %d, dcs = %d, ussdStr = %s", mode, dcs, ussdStr);
        goto error;
    }

    ascToHex(ussdStr, strlen(ussdStr), hex_buf);
    asprintf(&cmd, "AT+CUSD=%d,\"%s\",%d", mode, hex_buf, dcs);
    
    err = at_send_command(cmd, &p_response);
    if (err < 0 || p_response->success == 0) {
        goto error;
    }

    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
    free(cmd);
/* Android added by GanHuiliang_2012-11-9 *** BEGIN */
    at_response_free(p_response);
    p_response = NULL;
/* Android added by GanHuiliang_2012-11-9 END */
    return;

error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    free(cmd);
/* Android added by GanHuiliang_2012-11-9 *** BEGIN */
    at_response_free(p_response);
    p_response = NULL;
/* Android added by GanHuiliang_2012-11-9 END */
}

void ril_request_cancel_ussd(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ril_handle_cmd_default_response("AT+CUSD=2", token);
}

void ril_request_set_clir(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    char    cmdString[MAX_AT_LENGTH];
    int i = ((int   *) data)[0];
    sprintf(cmdString, "AT+CLIR=%d", i);
    ril_handle_cmd_default_response(cmdString, token);
}

void ril_request_set_call_forward(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);
		int ret =0;
    char    cmdString[MAX_AT_LENGTH];
    RIL_CallForwardInfo *info = (RIL_CallForwardInfo    *) data;
    int class = info->serviceClass;
    /* if class is missing, change it to BS_ALL */
    if (class == 0) class = 7;
    
    if(NULL == info->number){
    	  info->number = "";
    	  ret = 1;
    }
    
    sprintf(cmdString, "AT+CCFC=%d,%d,\"%s\",%d,%d", info->reason, info->status, info->number, info->toa, class);
		if(ret == 1) info->number = NULL;
    if (info->timeSeconds > 0)
    {
        char    time[10];
        sprintf(time, ",,,%d", info->timeSeconds);
        strcat(cmdString, time);
    }
    ril_handle_cmd_default_response(cmdString, token);
}

void ril_request_set_call_waiting(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    char    cmdString[MAX_AT_LENGTH];
    int action = ((int  *) data)[0];
    int class = ((int   *) data)[1];

    /* if class is missing, change it to BS_ALL */
    if (class == 0) class = 7;
    sprintf(cmdString, "AT+CCWA=1,%d,%d", action, class);
    ril_handle_cmd_default_response(cmdString, token);
}

void ril_request_change_barring_password(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    char    cmdString[MAX_AT_LENGTH];
    char    *code = ((char  **) data)[0];
    char    *oldPass = ((char   **) data)[1];
    char    *newPass = ((char   **) data)[2];

    sprintf(cmdString, "AT+CPWD=%s,%s,%s", code, oldPass, newPass);
    ril_handle_cmd_default_response_timeout(cmdString, token, TIMEOUT_CPWD);
}

void ril_request_query_clip(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ril_request_query_cxxx("AT+CLIP?", "+CLIP:", token);
}

void ril_request_set_supp_svc_notification(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    char    cmdString[MAX_AT_LENGTH];
    int flag = ((int    *) data)[0];
    sprintf(cmdString, "AT+CSSN=%d,%d", flag, flag);
    ril_handle_cmd_default_response(cmdString, token);
}
//add by liutao for dualmodem   start 
void ril_request_set_net_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);
	ATResponse *response = NULL;
	int err ,p1;
    char    cmdString[MAX_AT_LENGTH];
    char* cmd = ((char**)data)[0];	
    char* p = ((char**)data)[1];
	//p1=atoi(p);	
    sprintf(cmdString, "AT+ZSET=\"%s\",%s", cmd, p);
	ALOGD("%s entry: %s", __FUNCTION__, cmdString);	
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
void ril_request_get_net_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);
    char    cmdString[MAX_AT_LENGTH];
	ATResponse *response = NULL;
	int err = 0,p1;
	char *result,*p2;
    char* cmd = ((char**)data)[0];
	char *line = NULL;
    sprintf(cmdString, "AT+ZSET=\"%s\"", cmd);
	err = at_send_command_singleline(cmdString, "+ZSET:", &response);
  //  err = at_send_command(cmdString, response);
	if (err < 0 || response->success == 0)
    {
        goto error;
    }
	line = response->p_intermediates->line;
    err = at_tok_start(&line);
    DO_ERROR_JDUGE;
	err = at_tok_nextstr(&line, &result);
//	LOGD("%s entry: %s", __FUNCTION__, result);	
    DO_ERROR_JDUGE;
	err = at_tok_nextstr(&line, &p2);
//	LOGD("%s entry: %s", __FUNCTION__, p2);	
    DO_ERROR_JDUGE;
	RIL_onRequestComplete(token, RIL_E_SUCCESS, p2, sizeof(char));
	goto exit;
    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
	
}
void ril_request_set_tdband_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);
	int err = 0;
	ATResponse *response = NULL;
    char    cmdString[MAX_AT_LENGTH];
    int flag = ((int    *) data)[0];
    sprintf(cmdString, "AT+TDDBAND=%d", flag);
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
void ril_request_get_tdband_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);
	UNUSED(data);
	int err = 0,p;
	char *line = NULL;
	ATResponse *response = NULL;
	err = at_send_command_singleline("AT+TDDBAND?", "+TDDBAND:", &response);
    //err = at_send_command("AT+TDDBAND?", response);
	if (err < 0 || response->success == 0)
    {
        goto error;
    }
	line = response->p_intermediates->line;
    err = at_tok_start(&line);
    DO_ERROR_JDUGE;
	err = at_tok_nextint(&line, &p);
    DO_ERROR_JDUGE;
	RIL_onRequestComplete(token, RIL_E_SUCCESS, p, sizeof(int));
	goto exit;
    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}

/*
 * From RFC 2044:
 *
 * UCS-4 range (hex.)           UTF-8 octet sequence (binary)
 * 0000 0000-0000 007F   0xxxxxxx
 * 0000 0080-0000 07FF   110xxxxx 10xxxxxx
 * 0000 0800-0000 FFFF   1110xxxx 10xxxxxx 10xxxxxx
 * 0001 0000-001F FFFF   11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
 * 0020 0000-03FF FFFF   111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx
 * 0400 0000-7FFF FFFF   1111110x 10xxxxxx ... 10xxxxxx

  * UCS-2 range (hex.)           UTF-8 octet sequence (binary)
 * 0000-007F                      0xxxxxxx
 * 0080-07FF                      110xxxxx 10xxxxxx
 * 0800-FFFF                       1110xxxx 10xxxxxx 10xxxxxx

 */


char * libUCS2ToUTF8(char *UCS2String)
{
    unsigned int i = 0, len = 0,inBufLen = 0;
    char    *ret = NULL;
    if (UCS2String == NULL || strlen(UCS2String) % 2 != 0)
    {
        return NULL;
    }
    while (UCS2String[i] != 0 || UCS2String[i + 1] != 0)
    {
        i += 2;
    }
    inBufLen = i;
    for (i = 0; i < inBufLen; i += 2)
    {
        if ((UCS2String[i] == 0x00) && ((UCS2String[i + 1] & 0x80) == 0x00))
        {
            len += 1;
        }
        else if (UCS2String[i] < 0x08)
        {
            len += 2;
        }
        else if (((UCS2String[i] & 0xDC) == 0xD8))
        {
            if (((UCS2String[i + 2] & 0xDC) == 0xDC) && ((inBufLen - i) > 2))
            {
                i += 2;
                len += 4;
            }
            else
            {
                return NULL;
            }
        }
        else
        {
            len += 3;
        }
    }
    if (len > 0)
    {
        ret = malloc(sizeof(char) * (len + 1));
    }
    else
    {
        return NULL;
    }
    if (ret == NULL)
    {
        return NULL;
    }
    len = 0;
    for (i = 0; i < inBufLen; i += 2)
    {
        if ((UCS2String[i] == 0x00) && ((UCS2String[i + 1] & 0x80) == 0x00))
        {
            ret[len] = UCS2String[i + 1] & 0x7F;/* 0000-007F -> 0xxxxxx *//* 00000000 0abcdefg -> 0abcdefg */
            len += 1;
        }
        else if (UCS2String[i] < 0x08)
        {
            ret[len + 0] = 0xC0 | ((UCS2String[i] & 0x07) << 2) | ((UCS2String[i + 1] & 0xC0) >> 6);/* 0080-07FF -> 110xxxxx 10xxxxxx */            /* 00000abc defghijk -> 110abcde 10fghijk */
            ret[len + 1] = 0x80 | ((UCS2String[i + 1] & 0x3F) >> 0);
            len += 2;
        }
        else if ((UCS2String[i] & 0xDC) == 0xD8)
        {
            int abcde,    BCDE;
            if (((UCS2String[i + 2] & 0xDC) == 0xDC) && ((inBufLen - i) > 2))
            {
                free(ret);
                return NULL;
            }
                 BCDE = ((UCS2String[i] & 0x03) << 2) | ((UCS2String[i + 1] & 0xC0) >> 6); /* D800-DBFF DC00-DFFF -> 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx *//* 110110BC DEfghijk 110111lm nopqrstu ->{ Let abcde = BCDE + 1 } 11110abc 10defghi 10jklmno 10pqrstu */
                 abcde = BCDE + 1;
                 ret[len + 0] = 0xF0 | ((abcde & 0x1C) >> 2);
                 ret[len + 1] = 0x80 | ((abcde & 0x03) << 4) | ((UCS2String[i + 0 + 1] & 0x3C) >> 2);
            ret[len + 2] = 0x80 |
            ((UCS2String[i + 0 + 1] & 0x03) << 4) |
            ((UCS2String[i + 2 + 0] & 0x03) << 2) |
            ((UCS2String[i + 2 + 1] & 0xC0) >> 6);
                 ret[len + 3] = 0x80 | ((UCS2String[i + 2 + 1] & 0x3F) >> 0);
                 i += 2;
                 len += 4;
        }
        else
        {
            ret[len + 0] = 0xE0 | ((UCS2String[i] & 0xF0) >> 4);/* 0800-FFFF -> 1110xxxx 10xxxxxx 10xxxxxx */  /* abcdefgh ijklmnop -> 1110abcd 10efghij 10klmnop */
            ret[len + 1] = 0x80 | ((UCS2String[i] & 0x0F) << 2) | ((UCS2String[i + 1] & 0xC0) >> 6);
            ret[len + 2] = 0x80 | ((UCS2String[i + 1] & 0x3F) >> 0);
           len += 3;
        }
    }
    ret[len] = '\0';//    ALOGV("UTF8 string: len = %d, original len = %d ", len, inBufLen);
    return ret;
}


char * libIRAToBUF(char *IRAString)
{
    ALOGV("%s entry: %s", __FUNCTION__, IRAString);
    char    *ret = NULL;
    ret = malloc(sizeof(char) * (strlen(IRAString) / 2 + 2));
    int i = 0;
    unsigned int tmp;
    char    tmpbuf[3];
    int length = strlen(IRAString);
    while (i < length)
    {
        memcpy(tmpbuf, &IRAString[i], 2);
        tmpbuf[2] = '\0';
        sscanf(tmpbuf, "%x", &tmp);
        ret[i / 2] = tmp;
        i += 2;
    }
    ret[length / 2] = '\0';
    ret[length / 2 + 1] = '\0';
    ALOGV("%s exit: %s", __FUNCTION__, ret);
    return ret;
}

void ril_request_play_dtmf_tone(int request, void *data, size_t datalen, RIL_Token token)
{
	UNUSED(request);
	UNUSED(datalen);
	
	char	cmdString[64];
	char	c = ((char	*) data)[0];
	int err = 0;
	ATResponse  *response = NULL;
	
	ALOGD("ril_request_play_dtmf_tone:%c", c);
		
	sprintf(cmdString, "AT+DTONE=%c", (int) c);
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

void handle_cssi(const char *s, const char *smsPdu)
{
    UNUSED(smsPdu);

    char    *line = NULL,
    *linesave = NULL;
    int err;
    RIL_SuppSvcNotification *response = alloca(sizeof(RIL_SuppSvcNotification));
    memset(response, 0, sizeof(RIL_SuppSvcNotification));
    response->notificationType = 0;

    line = strdup(s);
    linesave = line;

    err = at_tok_start(&line);
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextint(&line, &response->code);
    if (err < 0)
    {
        goto error;
    }

    if (at_tok_hasmore(&line))
    {
        err = at_tok_nextint(&line, &response->index);
        if (err < 0)
        {
            goto error;
        }
    }
    RIL_onUnsolicitedResponse(RIL_UNSOL_SUPP_SVC_NOTIFICATION, response, sizeof(RIL_SuppSvcNotification));

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

void handle_cssu(const char *s, const char *smsPdu)
{
    UNUSED(smsPdu);

    char    *line = NULL,
    *linesave = NULL;
    int err;
    RIL_SuppSvcNotification *response = alloca(sizeof(RIL_SuppSvcNotification));
    memset(response, 0, sizeof(RIL_SuppSvcNotification));
    response->notificationType = 1;

    line = strdup(s);
    linesave = line;

    err = at_tok_start(&line);
    DO_ERROR_JDUGE;

    err = at_tok_nextint(&line, &(response->code));
    DO_ERROR_JDUGE;

    if (at_tok_hasmore(&line))
    {
        at_tok_nextint(&line, &(response->index));
    }
    if (at_tok_hasmore(&line))
    {
        err = at_tok_nextstr(&line, &(response->number));
        DO_ERROR_JDUGE;

        err = at_tok_nextint(&line, &(response->type));
        DO_ERROR_JDUGE;
    }
    RIL_onUnsolicitedResponse(RIL_UNSOL_SUPP_SVC_NOTIFICATION, response, sizeof(RIL_SuppSvcNotification));

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

void handle_cusd(const char *s, const char *smsPdu)
{
    UNUSED(smsPdu);
    char    *line = NULL, *linesave = NULL;
    int err;
    char    *response[3] = {NULL};
 
    line = strdup(s);
    linesave = line;
    err = at_tok_start(&line);
    DO_ERROR_JDUGE;
    err = at_tok_nextstr(&line, &response[0]);
    DO_ERROR_JDUGE;
    err = at_tok_nextstr(&line, &response[1]);
	
    if (err < 0)
    {
        response[1] = NULL;
		response[2] = NULL;
        RIL_onUnsolicitedResponse(RIL_UNSOL_ON_USSD, response, sizeof(response));
        goto error;
    }
    err = at_tok_nextstr(&line, &response[2]);
    if (err < 0)
    {
        response[1] = NULL;
		response[2] = NULL;
        RIL_onUnsolicitedResponse(RIL_UNSOL_ON_USSD, response, sizeof(response));
    }
    else
    { 
        RIL_onUnsolicitedResponse(RIL_UNSOL_ON_USSD, response, sizeof(response));
    }   
	
error:
    if (linesave != NULL)
    {
        free(linesave);
    }
    return;//ALOGW("%s: Error parameter in ind msg: %s", __FUNCTION__, s);
}

void handle_zpbic(const char *s, const char *smsPdu)
{
	UNUSED(smsPdu);
	char  *line = NULL;    
	char  *linesave = NULL;
	int err = -1;
	int initResult = 0;
	int type = 0;
	int cmdLen = 0;
	line = strdup(s);
	linesave = line;
	err = at_tok_start(&line);
	if (err < 0)
	{
	  goto error;
	}
	err = at_tok_nextint(&line, &initResult);
	if (err < 0)
	{
	  goto error;
	}
	err = at_tok_nextint(&line, &type);
	if (err < 0)
	{
	  goto error;
	}

    if((type == 0)&& (initResult == 1))
	{
		enque(getWorkQueue(SERVICE_SS), init_sim_sms_full, NULL);
	}
    if((type == 1)&& (initResult == 1))
	{
		RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_SIM_STATUS_CHANGED, NULL, 0);
		RIL_onUnsolicitedResponse(RIL_UNSOL_SIM_PHONEBOOK_READY, NULL, 0);
		hasZpbicPbOkRecvd = 1;
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


