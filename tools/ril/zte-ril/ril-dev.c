/******************************************************************************
 * *(C) Copyright 2012 ZTE International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/

#include "at_tok.h"
#include "atchannel.h"
#include "zte-ril.h"
#include "ril-requestdatahandler.h"
#include "work-queue.h"


#undef WORKAROUND_FOR_DEMO

extern int is_ppp_enabled();
extern void disableEthInterface(int cid);
extern void disablePPPInterface(int cid);

/** Query CP power on status, Returns 1 if on, 0 if off, and -1 on error */
int isRadioOn()
{
    ATResponse  *p_response = NULL;
    int err;
    char    *line;
    char    ret = 0;

    err = at_send_command_singleline("AT+CFUN?", "+CFUN:", &p_response);
    if (err < 0 || p_response->success == 0)
    {
        // assume radio is off
        goto error;
    }

    line = p_response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextbool(&line, &ret);
    if (err < 0)
    {
        goto error;
    }

    at_response_free(p_response);
    return (int) ret;

    error:
    at_response_free(p_response);
    return -1;
}

/* Process AT reply of RIL_REQUEST_GET_IMEI */
void ril_request_get_imei(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ATResponse  *response = NULL;
	 char    *line,  *result;
	 int err = at_send_command_singleline("AT+CGSN", "+CGSN:", &response);
    //int err = at_send_command_numeric("AT+CGSN", &response);	
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
    err = at_tok_nextstr(&line, &result);
    if (err < 0)
    {
        goto error;
    }

    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, strlen(result));
    goto exit;

    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}

/****************************gelei 20170317************************/
void ril_request_sim_open_channel_get_ziccid(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

#if 0  //modified by Boris ,20170318
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
  
#else   

#if 0
     ATResponse *p_response = NULL;
   //int err = at_send_command_numeric("AT+ZICCID", &p_response);//ZICCID modified into CIMI by Boris ,20170318
    char  *cmdString;
    asprintf(&cmdString, "AT+CIMI");
   int err= at_send_command_singleline(cmdString, "+CIMI:", &p_response);

    if (err < 0 || p_response->success == 0) {
        RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);//RIL_E_GENERIC_FAILURE 20170318 Boris
    } else {
        RIL_onRequestComplete(token, RIL_E_SUCCESS,
            p_response->p_intermediates->line, sizeof(char *));
    }
    at_response_free(p_response);
    /****************chenjun 20170317***/
	ALOGD("sim_open_channel IS OK");
	/**************************************/
#else

#endif
	 RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);//The response must be RIL_E_REQUEST_NOT_SUPPORTED 20170318 Boris ???
	 	ALOGD("sim_open_channel response must be RIL_E_REQUEST_NOT_SUPPORTED for Allwinner 6.0");
#endif			
			
}

/*********************************************************************/
void ril_request_get_imeisv(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ATResponse  *response = NULL;
    char    *line, *result;

    int err = at_send_command_singleline("AT*CGSN?", "*CGSN:", &response);
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

    err = at_tok_nextstr(&line, &result);
    if (err < 0)
    {
        goto error;
    }

    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, strlen(result));
    goto exit;

    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}
void ril_request_del_sn_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);
    char  *cmdString;
	//char  *number = ((char  **) data)[0];
	//char number = ((char    *) data)[0];
    ATResponse  *response = NULL;
    char    *line, *result;
	asprintf(&cmdString, "AT^ZBOARD=000000000000");
	ALOGD("cmdstring :=  %s ",cmdString);
    int err = at_send_command(cmdString,  &response);
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
void ril_request_set_sn_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    //UNUSED(data);
    UNUSED(datalen);
    char  *cmdString;
	char  *number = ((char  **) data)[0];
	//char number = ((char    *) data)[0];
    ATResponse  *response = NULL;
    char    *line, *result;
	ALOGD("SNnumber :=%s",number);
	asprintf(&cmdString, "AT^ZBOARD=%s", number);
	ALOGD("cmdstring :=  %s ",cmdString);
    int err = at_send_command(cmdString,  &response);
    if (err < 0 || response->success == 0 )
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

void ril_request_get_sn_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ATResponse  *response = NULL;
    char    *line, *result;

    int err = at_send_command_singleline("AT^ZBOARD?", "^ZBOARD:", &response);
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

    err = at_tok_nextstr(&line, &result);
    if (err < 0)
    {
        goto error;
    }

    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, strlen(result));
    goto exit;

    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}

/**
 * RIL_REQUEST_BASEBAND_VERSION
 *
 * Return string value indicating baseband version, eg
 * response from AT+CGMR.
 */
void ril_request_baseband_version(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ATResponse  *response = NULL;
    int err;
    char    *line,
    *result;

    err = at_send_command_singleline("AT+CGMR", "+CGMR:", &response);
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

    err = at_tok_nextstr(&line, &result);
    if (err < 0)
    {
        goto error;
    }

    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, strlen(result));
    goto exit;

    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}


/**
 * RIL_REQUEST_QUERY_AVAILABLE_BAND_MODE
 *
 * Query the list of band mode supported by RF.
 *
 * See also: RIL_REQUEST_SET_BAND_MODE
 */
void ril_request_query_available_band_mode(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ATResponse  *response = NULL;
    int err;
    int gsmband,
    umtsband,
    result[20];
    char    *line;
    char    *mode;
    int count = 0;

    err = at_send_command_singleline("AT*BAND=?", "*BAND:", &response);
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

    err = at_tok_nextstr(&line, &mode);
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextint(&line, &gsmband);
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextint(&line, &umtsband);
    if (err < 0)
    {
        goto error;
    }


    if ((gsmband & GSMBAND_PGSM_900) && (gsmband & GSMBAND_DCS_GSM_1800) && (umtsband & UMTSBAND_BAND_1))
    {
        count++;
        result[count] = 1; //EURO band(GSM-900 / DCS-1800 / WCDMA-IMT-2000)
    }

    if ((gsmband & GSMBAND_GSM_850) &&
        (gsmband & GSMBAND_PCS_GSM_1900) &&
        (umtsband & UMTSBAND_BAND_5) &&
        (umtsband & UMTSBAND_BAND_2))
    {
        count++;
        result[count] = 2; //US band(GSM-850 / PCS-1900 / WCDMA-850 / WCDMA-PCS-1900)
    }

    if ((umtsband & UMTSBAND_BAND_1) && (umtsband & UMTSBAND_BAND_6))
    {
        count++;
        result[count] = 3; //JPN band(WCDMA-800 / WCDMA-IMT-2000)
    }

    if ((gsmband & GSMBAND_PGSM_900) &&
        (gsmband & GSMBAND_DCS_GSM_1800) &&
        (umtsband & UMTSBAND_BAND_5) &&
        (umtsband & UMTSBAND_BAND_1))
    {
        count++;
        result[count] = 4; //AUS band (GSM-900 / DCS-1800 / WCDMA-850 / WCDMA-IMT-2000)
    }

    result[0] = count;

    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, (count + 1) * sizeof(int));

    goto exit;

    error : RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit : at_response_free(response);
}

/* RIL_REQUEST_DEVICE_IDENTITY
 *
 * Request the device ESN / MEID / IMEI / IMEISV.
 *
 */
void ril_request_device_identity(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ATResponse  *response = NULL;
    int err;
    char    *line = NULL;
    char    *sv = NULL;
    char    *imei = NULL;
    char    *result[4] =
    {
        0
    };
    char    DeviceIdentity_sv[3] =
    {
        0
    }; // SVN of IMEISV, 2 digits + 1 '\0'

    err = at_send_command_singleline("AT*CGSN?", "*CGSN:", &response);
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

    err = at_tok_nextstr(&line, &sv);
    if (err < 0)
    {
        goto error;
    }

    strncpy(DeviceIdentity_sv, sv, 2);
    DeviceIdentity_sv[2] = '\0';

    at_response_free(response); 
    response = NULL;

    err = at_send_command_numeric("AT+CGSN", &response);
    if (err < 0 || response->success == 0 || response->p_intermediates == NULL)
    {
        goto error;
    }

    imei = response->p_intermediates->line;

    result[0] = imei;
    result[1] = DeviceIdentity_sv;
    result[2] = NULL;
    result[3] = NULL;

    ALOGI("%s: imei: %s, sv: %s\n", __FUNCTION__, imei, DeviceIdentity_sv);
    RIL_onRequestComplete(token, RIL_E_SUCCESS, result, sizeof(result));

    goto exit;
    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}

void ril_request_set_band_mode(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(data);
    UNUSED(datalen);

    // placeholder
    ALOGW("unsupported request: %d\n", request);
    RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
}

void ril_request_oem_hook_raw(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(data);
    UNUSED(datalen);

    // placeholder
    ALOGW("unsupported request: %d\n", request);
    RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
    char    oem_name[9];
    int index = 0;
    uint32_t    cmd_id = 0;
    uint32_t    payload_length = 0;
    int continue_flag = 1;
    ATResponse  *response = NULL;
    int err;
    int result[2];
    char    *line;
    /* decode the raw string to find out the oem name string data[0 - 7], 8 bytes*/
    if (strncmp(data, RIL_HOOK_OEM_NAME, RILHOOK_OEM_NAME_LENGTH) != 0)
    {
        memcpy(oem_name, &data[index], RILHOOK_OEM_NAME_LENGTH);
        oem_name[RILHOOK_OEM_NAME_LENGTH] = '\0';
        //ALOG( "Mismatch in oem_name between received=%s and expected=%s \n", oem_name, RIL_HOOK_OEM_NAME);
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
        continue_flag = 0;
    }

    /* incrementing the index by OEM name size i.e 9 bytes */
    index += (int) RILHOOK_OEM_NAME_LENGTH;

    /* decode the raw string to find out command id, data[9 - 12], 4 bytes */
    memcpy(&cmd_id, &data[index], RILHOOK_OEM_REQUEST_ID_LEN);
    if (cmd_id >= RILHOOK_MAX)
    {
        //ALOG( "Received un expected command id = %d\n", cmd_id );
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
        //continue_flag = 0;
    }

    /* incrementing the index by command id size i.e 4 bytes */
    index += (int) RILHOOK_OEM_REQUEST_ID_LEN;

    /* decode the raw string to find the length of the payload, data[13 - 16],  4 bytes*/
    memcpy(&payload_length, &data[index], RILHOOK_OEM_REQUEST_DATA_LEN);
    index += (int) RILHOOK_OEM_REQUEST_DATA_LEN;

    //ALOG( "Received command id = %d and payload length = %d\n", cmd_id, payload_length );

    switch (cmd_id)
    {
        case RILHOOK_SET_AUDIOPATH:
        {
            uint32_t    PCM_Mode;
            memcpy(&PCM_Mode, &data[index], 4);
            //MYLOG( "RILHOOK_SET_AUDIOPATH PCM_Enable= %d\n", PCM_Mode );
            //requestSetAudioPath(PCM_Enable,t);
            if (PCM_Mode == 0)//receiver
            {
                //  strcpy(cmdString, "AT^DAUDSCS=1");
                err = at_send_command_timeout("AT^DAUDSCS=1", &response, TIMEOUT_COPS);
            }
            else if (PCM_Mode == 1)//BT
            {
                //strcpy(cmdString, "AT^DAUDSCS=4");
                err = at_send_command_timeout("AT^DAUDSCS=4", &response, TIMEOUT_COPS);
            }
            else if (PCM_Mode == 2)//HeadSet
            {
                //strcpy(cmdString, "AT^DAUDSCS=3");
                err = at_send_command_timeout("AT^DAUDSCS=3", &response, TIMEOUT_COPS);
            }
            else if (PCM_Mode == 3)
            {
                //strcpy(cmdString, "AT^DAUDSCS=2");
                err = at_send_command_timeout("AT^DAUDSCS=2", &response, TIMEOUT_COPS);
            }
            else if (PCM_Mode == 4)
            {
                //strcpy(cmdString, "AT^DAUDAECON=1");
                err = at_send_command_timeout("AT^DAUDAECON=1", &response, TIMEOUT_COPS);
            }
            else if (PCM_Mode == 5)
            {
                //strcpy(cmdString, "AT^DAUDAECON=0");
                err = at_send_command_timeout("AT^DAUDAECON=0", &response, TIMEOUT_COPS);
            }
        }
        if (err < 0 || response->success == 0)
        {
            RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
        }
        else
        {
            RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
        }           
        break;
        default:
        //MYLOG( "Received invalid command id = %d\n", cmd_id );
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    }
}

#if 0
/*void ril_request_oem_hook_strings(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(data);
    UNUSED(datalen);

    // placeholder
    ////ALOGW("unsupported request: %d\n", request);
    //RIL_onRequestComplete(token, RIL_E_REQUEST_NOT_SUPPORTED, NULL, 0);
    //todo
            int i;
            const char ** cur;
            //MYLOG("got OEM_HOOK_STRINGS: 0x%8p %lu", data, (long)datalen);

            for (i = (datalen / sizeof (char *)), cur = (const char **)data ; i > 0 ; cur++, i --) {
                //MYLOG("REQUEST_OEM_HOOK_STRINGS > '%s'", *cur);
            }
            RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
            /**expectedTime = 1;*/ /*no more at command, not need*/
            //continue_flag = 0;
        
//}*/
#endif
void ril_request_oem_hook_strings(int request, void *data, size_t datalen, RIL_Token token)
{
    int i;
    const char  **cur;

    char    sendCmd[512] =
    {
        0
    };
    char    resPrefix[20] =
    {
        0
    };
    char    *response[3];
	char    *result[16];
    //UNUSED(data);
    //UNUSED(datalen);

    // placeholder

    memset(response, 0, sizeof(response));
    ALOGW("unsupported request: %d\n", request);
    ALOGW("zxj unsupported request: %s\n", data);
    ALOGW("zxj unsupported request: %d\n", datalen);

    ALOGD("zxj got OEM_HOOK_STRINGS: 0x%8p %lu", data, (long) datalen);

    for (i = (datalen / sizeof(char*)), cur = (const char**) data ; i > 0 ; cur++, i --)
    {
        ALOGD("> '%s'  i = %d", *cur, i);

        if ((**cur != 0) && (sendCmd[0] == 0))
        {
            memcpy(sendCmd, *cur, strlen(*cur));
        }
        else if ((**cur != 0) && (resPrefix[0] == 0))
        {
            memcpy(resPrefix, *cur, strlen(*cur));
        }
    }
    if ((resPrefix[0] != 0) && (sendCmd[0] != 0))//have Prefix
    {
        ATResponse  *p_response = NULL;
        char    *line;

        int err = at_send_command_singleline(sendCmd, resPrefix, &p_response);
        if (err < 0 || p_response->success == 0)
        {
            RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
            ALOGW("zxj : RIL_E_GENERIC_FAILURE have Prefix\n");
        }
        else
        {
            // decode to be added
            line = p_response->p_intermediates->line;

            //memcpy(response[0], line, strlen(line));
            ALOGW("zxj : RIL_E_SUCCESS line=%s. \n", line);
            ALOGW("zxj : RIL_E_SUCCESS response=%s. \n", response[0]);
            err = at_tok_start(&line);
            if (err < 0)
            {
                ALOGW("zxj : RIL_E_SUCCESS err=%d. line=%s. \n", err, line);

                RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
                ALOGW("zxj : RIL_E_GENERIC_FAILURE have Prefix\n");
            }
            else
            {
			   if (resPrefix[0] ="+BGLTEPLMN"){
					err = at_tok_nextstr(&line, &response[0]);
					if (err < 0) {
						RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
					}
					err = at_tok_nextstr(&line, &response[1]);
					if (err < 0) {
					RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
					}
					err = at_tok_nextstr(&line, &response[2]);
					if (err < 0) {
					RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
					}
					ALOGW("zxj : at_tok_nextstr result1 = %s,at_tok_nextstr result2 = %s,at_tok_nextstr result3 = %s", response[0],response[1],response[2]);
					RIL_onRequestComplete(token, RIL_E_SUCCESS, response, sizeof(response));
				}
				else{
				//RIL_onRequestComplete(token, RIL_E_SUCCESS, line, strlen(line));

                //err = at_tok_nextstr(&line, &(response[0]));
                RIL_onRequestComplete(token, RIL_E_SUCCESS, &response, sizeof(response));
                }
            }
        }
        at_response_free(p_response);
    }
    else if (sendCmd[0] != 0)//have no Prefix
    {
        ATResponse  *p_response = NULL;

        int err = at_send_command(sendCmd, &p_response);
        if (err < 0 || p_response->success == 0)
        {
            RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
            ALOGW("zxj : RIL_E_GENERIC_FAILURE no Prefix\n");
        }
        else
        {
            RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
            ALOGW("zxj : RIL_E_SUCCESS  no Prefix\n");
        }
        at_response_free(p_response);
    }
}

#ifdef ZTE_RIL_EXTEND
void ril_request_engineer_mode_cmd_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    int err = -1;
    /*
        if(RIL_REQUEST_SET_GSM_INFO_PERIODIC_MODE == request)
        {
            err = at_send_command("AT+EEMOPT=2,1", &response);
        }
        else if(RIL_REQUEST_TURN_OFF_GSM_INFO_INDICATOR == request)
        {
            err = at_send_command("AT+EEMOPT=0", &response);
        }
        else if(RIL_REQUEST_SET_GSM_INFO_QUERY_MODE == request)
        {
            err = at_send_command("AT+EEMOPT=1", &response);
        }
        else
        {
            ALOGE("%s: unknown request:%d!!\n", __FUNCTION__, request);
            goto error;
        }
        if (err < 0 || response->success == 0)
            goto error;
        RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
        goto exit;
    error:
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    */
    at_response_free(response);
}

void ril_request_gsm_information_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    int responseInt[2];

    int err = at_send_command_singleline("AT+EEMGINFO?", "+EEMGINFO:", &response);
    if (err < 0 || response->success == 0 || !response->p_intermediates)
    {
        goto error;
    }

    char    *line = response->p_intermediates->line;
    err = at_tok_start(&line);
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextint(&line, &(responseInt[0]));
    if (err < 0)
    {
        goto error;
    }
    err = at_tok_nextint(&line, &(responseInt[1]));
    if (err < 0)
    {
        goto error;
    }

    RIL_onRequestComplete(token, RIL_E_SUCCESS, responseInt, sizeof(responseInt));
    goto exit;

    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}

/*void ril_request_handle_cp_reset_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    int err = -1;
    char    cmdString[128];

    if (RIL_REQUEST_SET_CP_RESET == request)
    {
        int resetFlag = ((int   *) data)[0];
        snprintf(cmdString, sizeof(cmdString), "AT+ZCPR=%d", resetFlag);
        err = at_send_command(cmdString, &response);
    }
    else
    {
        ALOGE("%s: unknown request:%d!!\n", __FUNCTION__, request);
        goto error;
    }
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

void ril_request_handle_imei_cmd_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    char    cmdString[MAX_AT_LENGTH];
    int err = -1;

    if (RIL_REQUEST_SET_IMEI == request)
    {
        char    *imei = ((char  **) data)[0];
        snprintf(cmdString, sizeof(cmdString), "AT*MRD_IMEI=W,0201,13DEC2010,%s", imei);
        err = at_send_command(cmdString, &response);
    }
    else if (RIL_REQUEST_DEL_IMEI == request)
    {
        err = at_send_command("AT*MRD_IMEI=D", &response);
    }
    else
    {
        ALOGE("%s: unknown request:%d!!\n", __FUNCTION__, request);
        goto error;
    }
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

void ril_request_handle_sn_cmd_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    char    cmdString[MAX_AT_LENGTH];
    int err = -1;

    if (RIL_REQUEST_SET_SN == request)
    {
        char    *sn_str = ((char    **) data)[0];       
        snprintf(cmdString, sizeof(cmdString), "AT*MRD_SN=W,0201,13DEC2010,%s", sn_str);
        err = at_send_command(cmdString, &response);
    }
    else if (RIL_REQUEST_GET_SN == request)
    {
        err = at_send_command_singleline("AT*MRD_SN?", "*MRD_SN:", &response);
    }
    else if (RIL_REQUEST_DEL_SN == request)
    {
        err = at_send_command("AT*MRD_SN=D", &response);
    }
    else
    {
        ALOGE("%s: unknown request:%d!!\n", __FUNCTION__, request);
        goto error;
    }
    if (err < 0 || response->success == 0)
    {
        goto error;
    }

    if (RIL_REQUEST_GET_SN == request)
    {
        if (!response->p_intermediates)
        {
            goto error;
        }

        char    *strSn = response->p_intermediates->line;
        RIL_onRequestComplete(token, RIL_E_SUCCESS, strSn, sizeof(strSn));
    }
    else
    {
        RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
    }
    goto exit;

    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}

void ril_request_handle_cal_cmd_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    char    cmdString[MAX_AT_LENGTH];
    int err = -1;
    char    *calFileName[] =
    {
        "TdCalData.nvm",
        "GsmCalData.nvm"
    };
    int isTdGsm = ((int *) data)[0];  //0 - TD, 1 - GSM

    if (RIL_REQUEST_SET_CAL == request)
    {
        snprintf(cmdString, sizeof(cmdString), "at*mrd_cdf=w,%s,0201,25dec2010", calFileName[isTdGsm]);
    }
    else if (RIL_REQUEST_GET_CAL == request)
    {
        snprintf(cmdString, sizeof(cmdString), "at*mrd_cdf=q,%s", calFileName[isTdGsm]);
    }
    else if (RIL_REQUEST_DEL_CAL == request)
    {
        snprintf(cmdString, sizeof(cmdString), "at*mrd_cdf=d,%s", calFileName[isTdGsm]);
    }
    else
    {
        ALOGE("%s: unknown request:%d!!\n", __FUNCTION__, request);
        goto error;
    }
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

void ril_request_handle_adjust_cmd_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    int err = -1;

    if (RIL_REQUEST_GET_ADJUST == request)
    {
        err = at_send_command_singleline("AT^ZADJUST?", "^ZADJUST:", &response);
    }
    else
    {
        ALOGE("%s: unknown request:%d!!\n", __FUNCTION__, request);
        goto error;
    }
    if (err < 0 || response->success == 0)
    {
        goto error;
    }

    if (RIL_REQUEST_GET_ADJUST == request)
    {
        if (!response->p_intermediates)
        {
            goto error;
        }

        char    *strAdjust = response->p_intermediates->line;
        RIL_onRequestComplete(token, RIL_E_SUCCESS, strAdjust, sizeof(strAdjust));
    }
    goto exit;

    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}

void ril_request_handle_radio_cmd_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    char    cmdString[128];
    int err = -1;

    if (RIL_REQUEST_SET_CFUN == request)
    {
        int cfunFlag = ((int    *) data)[0];
        snprintf(cmdString, sizeof(cmdString), "AT+CFUN=%d", cfunFlag);
        err = at_send_command(cmdString, &response);
    }
    else
    {
        ALOGE("%s: unknown request:%d!!\n", __FUNCTION__, request);
        goto error;
    }
    if (err < 0 || response->success == 0)
    {
        goto error;
    }

    if (RIL_REQUEST_SET_CFUN == request)
    {
        RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
    }
    goto exit;

    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    exit:
    at_response_free(response);
}

void ril_request_handle_sensor_cmd_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    ATResponse  *response = NULL;
    char    cmdString[MAX_AT_LENGTH];
    int err = -1;

    if (RIL_REQUEST_SET_SENSOR_CAL == request)
    {
        err = at_send_command("at*mrd_cdf=w,SensorCalData.conf", &response);
    }
    else if (RIL_REQUEST_GET_SENSOR_CAL == request)
    {
        err = at_send_command("at*mrd_cdf=q,SensorCalData.conf", &response);
    }
    else if (RIL_REQUEST_DEL_SENSOR_CAL == request)
    {
        err = at_send_command("at*mrd_cdf=d,SensorCalData.conf", &response);
    }
    else
    {
        ALOGE("%s: unknown request:%d!!\n", __FUNCTION__, request);
        goto error;
    }
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
}*/
#endif /* ZTE_RIL_EXTEND */

