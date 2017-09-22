/******************************************************************************
 * *(C) Copyright 2012 ZTE International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/
#include <sys/atomics.h>
#include <cutils/properties.h>
#include "at_tok.h"
#include "atchannel.h"
#include "zte-ril.h"
#include "misc.h"
#include "ril-requestdatahandler.h"
#include "work-queue.h"

static int  suppress_dtmf = 0;

static int  current_call_num = 0; 

static int  ring = 0;

int intermediate_result;

extern int dtmf_end;
static int convertClccStateToRILState(int state, RIL_CallState *p_state)
{
    switch (state)
    {
        case 0:
        *p_state = RIL_CALL_ACTIVE;   return 0;
        case 1:
        *p_state = RIL_CALL_HOLDING;  return 0;
        case 2:
        *p_state = RIL_CALL_DIALING;  return 0;
        case 3:
        *p_state = RIL_CALL_ALERTING; return 0;
        case 4:
        *p_state = RIL_CALL_INCOMING; return 0;
        case 5:
        *p_state = RIL_CALL_WAITING;  return 0;
        /* --TIOMAP4-Weiyide136124--2012-9-21--Begin */
        /*** Note:  TSP Modem extend 7,8 state,
                           7is Call connecting after ATA
                           8 is Call disconnecting after AT+CHLD=1x***/
        case 7:
        *p_state = RIL_CALL_INCOMING;  return 0;
        case 8:
        return -1;  
        /* --TIOMAP4-Weiyide136124--2012-9-21--End */
        default:
        return -1;
    }
}

/**
 * Note: directly modified line and has *p_call point directly into
 * modified line. AT response format like:
 *  +CLCC: 1,0,2,0,0,\"+18005551212\",145,"Name"
 *        index,isMT,state,mode,isMpty(,number,TOA(,alpha))?
 */
static int getInfoFromClccLine(char *line, RIL_Call *p_call)
{
    int err,
    state,
    mode;

    err = at_tok_start(&line);
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextint(&line, &(p_call->index));
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextbool(&line, &(p_call->isMT));
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextint(&line, &state);
    if (err < 0)
    {
        goto error;
    }

    err = convertClccStateToRILState(state, &(p_call->state));
    if (err < 0)
    {
        goto error;
    }

    err = at_tok_nextint(&line, &mode);
    if (err < 0)
    {
        goto error;
    }

    p_call->isVoice = (mode == 0);

    err = at_tok_nextbool(&line, &(p_call->isMpty));
    if (err < 0)
    {
        goto error;
    }

    ALOGD("%s: index: %d, isMT: %d, state: %d, isVoice: %d, isMpty: %d\n", __FUNCTION__, p_call->index, p_call->isMT, p_call->state,
          p_call->isVoice, p_call->isMpty);

    if (at_tok_hasmore(&line))
    {
        err = at_tok_nextstr(&line, &(p_call->number));
        /* tolerate null here */
        if (err < 0)
        {
            return 0;
        }

        if ((p_call->number != NULL) && (0 == strspn(p_call->number, "+0123456789")))
        {
            p_call->number = NULL;
        }

        err = at_tok_nextint(&line, &p_call->toa);
        if ((err < 0) && (p_call->number != NULL))
        {
            goto error;
        }

        ALOGD("%s: number: %s, toa: %d\n", __FUNCTION__, ((p_call->number == NULL) ? "null" : p_call->number), p_call->toa);

        if (at_tok_hasmore(&line))
        {
            err = at_tok_nextstr(&line, &p_call->name);
            if (err < 0)
            {
                goto error;
            }
            ALOGD("%s: name: %s\n", __FUNCTION__, ((p_call->name == NULL) ? "null" : p_call->name));
        }
    }

    return 0;

    error : ALOGE("invalid CLCC line\n");
    return -1;
}


/* Process AT reply of RIL_REQUEST_GET_CURRENT_CALLS */
void ril_request_get_current_calls(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ATResponse  *response = NULL;
    ATLine  *p_cur;
    int countCalls;
    int countValidCalls;
    RIL_Call    *p_calls;
    RIL_Call    **pp_calls;
    int i,
    err;
    int needRepoll = 0;
    RIL_RadioState  currentState = getRadioState();

    /* Android will call +CLCC to clean variable when RADIO_OFF, return radio_available directly */
    if (currentState == RADIO_STATE_SIM_NOT_READY)
    {
        RIL_onRequestComplete(token, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
        goto exit;
    }

    err = at_send_command_multiline("AT+CLCC", "+CLCC:", &response);
	if (err < 0 )
    {
        ALOGD("ril_request_get_current_calls  err= %d", err);
		RIL_onRequestComplete(token, RIL_E_RADIO_NOT_AVAILABLE, NULL, 0);
		dtmf_end = 0;
		at_response_free(response);
		return;
    }
	
    if (response->success == 0)
    {
        goto error;
    }

    /* count the calls */
    for (countCalls = 0, p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next)
    {
        countCalls++;
    }

    /* yes, there's an array of pointers and then an array of structures */
    pp_calls = (RIL_Call * *) alloca(countCalls * sizeof(RIL_Call *));
    p_calls = (RIL_Call *) alloca(countCalls * sizeof(RIL_Call));
    memset(p_calls, 0, countCalls * sizeof(RIL_Call));

    /* init the pointer array */
    for (i = 0; i < countCalls; i++)
    {
        pp_calls[i] = &(p_calls[i]);
    }

    /* Analyze AT response and report */
    for (countValidCalls = 0, p_cur = response->p_intermediates; p_cur != NULL; p_cur = p_cur->p_next)
    {
        err = getInfoFromClccLine(p_cur->line, p_calls + countValidCalls);
        if (err != 0)
        {
            continue;
        }

        countValidCalls++;
    }

    current_call_num = countValidCalls;

    RIL_onRequestComplete(token, RIL_E_SUCCESS, pp_calls, countValidCalls * sizeof(RIL_Call *));
	dtmf_end = 0;
    goto exit;
    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	dtmf_end = 0;
    exit:
    at_response_free(response);
}

void ril_request_dial(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(datalen);

    ATResponse  *response = NULL;
    RIL_Dial    *p_dial = (RIL_Dial *) data;
    char    cmd[128];

    if (request == RIL_REQUEST_DIAL)
    {
        const char  *clir;
        switch (p_dial->clir)
        {
            case 1:
            clir = "I"; /*invocation*/
            break;
            case 2:
            clir = "i"; /*suppression*/
            break;
            case 0:
            default:
            clir = ""; /*subscription default*/
        }
        snprintf(cmd, sizeof(cmd), "ATD%s%s;", p_dial->address, clir);
    } 
    else
    {
        goto error;
    }

    int err = at_send_command(cmd, &response);
    if (err < 0 || response->success == 0)
    {
        goto error;
    }
    RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
	dtmf_end = 0;
    goto exit;
    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	dtmf_end = 0;
    exit:
    at_response_free(response);
}



/*
 void ril_request_last_call_fail_cause(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ril_handle_cmd_one_int("AT+CEER", "+CEER:", token);
    }
*/  
void ril_request_last_call_fail_cause(int request, void *data, size_t datalen, RIL_Token token)
{
    int result,
    err;
    char    *line;
    char    *cause = NULL;

    ATResponse  *response = NULL;
    ATLine  *p_cur;
    int countCalls;
    int countValidCalls;
    RIL_Call    *p_calls;
    RIL_Call    **pp_calls;


    err = at_send_command_multiline("AT+CEER", "+CEER:", &response);

	if(err < 0)
	{
       goto error;
	}

    if (response->success == 0)
    {
        goto error;
    }

    if (!response->p_intermediates)
    {
        goto error;
    }

    line = response->p_intermediates->line;

    err = at_tok_start(&line);
    if (err < 0)
    {
        goto error;
    }

    if ((cause = strstr(line, "FDN blocked")) != NULL)
    {
        result = CALL_FAIL_FDN_BLOCKED;
    }
    else
    {
        result = intermediate_result;
    }

    RIL_onRequestComplete(token, RIL_E_SUCCESS, &result, sizeof(result));
    //MYLOG("ECEER:result==%d", result);
    dtmf_end = 0;
    return 1;

    error:
    RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
	dtmf_end = 0;
    return 1;
}

void ril_request_answer(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ril_handle_cmd_default_response("ATA", token);
	dtmf_end = 0;
}

void ril_request_hangup(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    char    cmdString[64];
    sprintf(cmdString, "AT+CHLD=1%d", *(int *) data);
    ril_handle_cmd_default_response_timeout(cmdString, token, TIMEOUT_CHLD);
	dtmf_end = 0;
}

void ril_request_hangup_waiting_or_background(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ALOGD("%s: the current call num is %d\n", __FUNCTION__, current_call_num);
    if (current_call_num < 2)
    {
        ril_handle_cmd_default_response_timeout("ATH", token, TIMEOUT_CHLD);
    }
    else
    {
        ril_handle_cmd_default_response_timeout("AT+CHLD=0", token, TIMEOUT_CHLD);
    }
	dtmf_end = 0;
}

void ril_request_hangup_foreground_resume_background(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ril_handle_cmd_default_response_timeout("AT+CHLD=1", token, TIMEOUT_CHLD);
	dtmf_end = 0;
}

void ril_request_switch_waiting_or_holding_and_active(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ril_handle_cmd_default_response_timeout("AT+CHLD=2", token, TIMEOUT_CHLD);
	dtmf_end = 0;
}

void ril_request_conference(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ril_handle_cmd_default_response_timeout("AT+CHLD=3", token, TIMEOUT_CHLD);
	dtmf_end = 0;
}

void ril_request_udub(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    //ril_handle_cmd_default_response_timeout("AT+CHLD=0", token, TIMEOUT_CHLD);
    ril_handle_cmd_default_response_timeout("ATH", token, TIMEOUT_CHLD);
	dtmf_end = 0;
}

void ril_request_dtmf(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    char    cmdString[64];
	char ret[PROPERTY_VALUE_MAX];
	property_get("ril.dtmf", ret, "1");
	if(0 == strcmp(ret,"0"))
    {
        RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
		return;
	} 
    if (suppress_dtmf)
    {
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    }
    else
    {
        char    c = ((char  *) data)[0];
        char    duration = ((char   *) data)[1];
        ALOGD("RIL_REQUEST_DTMF:%c,%c", c, duration);
        /* Android added by GanHuiliang_2012-8-29 *** BEGIN dtmf string*/
        sprintf(cmdString, "AT+VTS=%c", (int) c);
        //sprintf(cmdString, "AT+VTS=\"%c\"", (int)c);
        /* Android added by GanHuiliang_2012-8-29 END */
        ril_handle_cmd_default_response(cmdString, token);
    }
}

void ril_request_dtmf_start(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    char    cmdString[64];
	char ret[PROPERTY_VALUE_MAX];
	property_get("ril.dtmf", ret, "1");
	if((0 == strcmp(ret,"0"))||(dtmf_end == 1))
    {
        RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
		return;
	}
    if (suppress_dtmf)
    {
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    }
    else
    {
        char    c = ((char  *) data)[0];
        ALOGD("RIL_REQUEST_DTMF_START:%c", c);
        /* Android added by GanHuiliang_2012-8-29 *** BEGIN dtmf string*/
        sprintf(cmdString, "AT+VTS=%c", (int) c);
        //sprintf(cmdString, "AT+VTS=\"%c\"", (int)c);
        /* Android added by GanHuiliang_2012-8-29 END */
        ril_handle_cmd_default_response(cmdString, token);
    }
}

void ril_request_dtmf_stop(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    if ((suppress_dtmf)||(dtmf_end == 1))
    {
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    }
    else 
            /* Android added by GanHuiliang_2012-8-29 *** BEGIN */
    {
        ril_handle_cmd_default_response("AT", token);
    }
    //RIL_onRequestComplete(token, RIL_E_SUCCESS, NULL, 0);
    /* Android added by GanHuiliang_2012-8-29 END */
}

/*
+ZMUT=<channel>, <mute_switch>
<channel>
    0-uplink, 1-downlink, 2-both
<mute_switch>
    0-disable,  1-enable
*/
void ril_request_set_mute(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    char    cmdString[64];
    int mute = ((int    *) data)[0];  //0-mute off, 1-mute on
    if (0 == mute || 1 == mute)
    {
        sprintf(cmdString, "AT+ZMUT=0,%d", mute);
        ril_handle_cmd_default_response(cmdString, token);
    }
    else
    {
        RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    }
	dtmf_end = 0;
}

void ril_request_get_mute(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    //RIL_onRequestComplete(token, RIL_E_GENERIC_FAILURE, NULL, 0);
    ril_handle_cmd_one_int("AT+CMUT?", "+CMUT:", token);  // TSP modem do not support query
    dtmf_end = 0;
}

void ril_request_separate_connection(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(datalen);

    char    cmdString[64];
    int party = ((int   *) data)[0];
    // Make sure that party is in a valid range.
    // (Note: The Telephony middle layer imposes a range of 1 to 7.
    // It's sufficient for us to just make sure it's single digit.)
    if (party <= 0 || party >= 10)
    {
        party = 1;
    }
    sprintf(cmdString, "AT+CHLD=2%d", party);
    ril_handle_cmd_default_response_timeout(cmdString, token, TIMEOUT_CHLD);
	dtmf_end = 0;
}

void ril_request_explicit_call_transfer(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(request);
    UNUSED(data);
    UNUSED(datalen);

    ril_handle_cmd_default_response_timeout("AT+CHLD=4", token, TIMEOUT_CHLD);
	dtmf_end = 0;
}

void cc_pre_process_request(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(data);
    UNUSED(datalen);
    UNUSED(token);

    if (request == RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND ||
        request == RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE ||
        request == RIL_REQUEST_CONFERENCE)
    {
        __atomic_inc(&suppress_dtmf);
    }
}

void cc_post_process_request(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(data);
    UNUSED(datalen);
    UNUSED(token);

    if (request == RIL_REQUEST_HANGUP_FOREGROUND_RESUME_BACKGROUND ||
        request == RIL_REQUEST_SWITCH_WAITING_OR_HOLDING_AND_ACTIVE ||
        request == RIL_REQUEST_CONFERENCE)
    {
        __atomic_dec(&suppress_dtmf);
    }
}

//add by ax for TSP 7510 modem begin
void ril_request_play_callwait_tone_zte(int request, void *data, size_t datalen, RIL_Token token)
{
    UNUSED(data);
    UNUSED(datalen);
    
    int err;
    char cmdString[64];
    char c = ((char *)data)[0];
    
	ALOGD("ril_request_play_callwait_tone_zte val_tone = %c",c);
    sprintf(cmdString, "AT+ZTONE=%c", c);
    ril_handle_cmd_default_response(cmdString, token);
	dtmf_end = 0;
  
}
//add by ax for TSP 7510 modem end

void handle_call_state_changed(const char *s, const char *smsPdu)
{
    UNUSED(s);
    UNUSED(smsPdu);
    if ((4 != onUnsolicitedreponseCC(s))&&(0 == ring))
    {
        RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED, NULL, 0);
    }
}
int onUnsolicitedreponseCC(const char *s)
{
    char    *line = NULL;
    char    *linesave = NULL;
    int err = -1;
    int state = 0;
    int cause = 0;
    char    *skip = NULL;
    int skip_int = 0;
    if (!strStartsWith(s, "^DSCI:"))
    {
        return 0;
    }

    line = strdup(s);
    linesave = line;
    err = at_tok_start(&line);
    if (err < 0)
    {
        goto done;
    }  
    err = at_tok_nextint(&line, &skip_int);
    if (err < 0)
    {
        goto done;
    }  
    err = at_tok_nextint(&line, &skip_int);
    if (err < 0)
    {
        goto done;
    }  
    err = at_tok_nextint(&line, &state);
    if (err < 0)
    {
        goto done;
    }  
    err = at_tok_nextstr(&line, &skip);
    if (err < 0)
    {
        goto done;
    }  
    err = at_tok_nextstr(&line, &skip);
    if (err < 0)
    {
        goto done;
    }  
    err = at_tok_nextstr(&line, &skip);
    if (err < 0)
    {
        goto done;
    }  
    err = at_tok_nextstr(&line, &skip);
    if (err < 0)
    {
        goto done;
    }  
    err = at_tok_nextstr(&line, &skip);
    if (err < 0)
    {
        goto done;
    }  
    err = at_tok_nextint(&line, &cause);
    if (err < 0)
    {
        goto done;
    }  
    //MYLOG("onUnsolicitedreponseCC state = %d, cause = %d, err = %d",state,cause, err);

    done:
    /* Free allocated memory and return */
    if (linesave != NULL)
    {
        free(linesave);
    }

    //MYLOG("done:: state = %d, cause = %d, err = %d",state,cause, err);
    if (6 == state && err == 0)/*Terminated*/
    {
        intermediate_result = cause;
    }
    else
    {
        intermediate_result = 0;
    }
    return state;
}


void handle_ring(const char *s, const char *smsPdu)
{
    UNUSED(s);
    UNUSED(smsPdu);

	ring = 1;
	ALOGD("handle_ring : ring = %d",ring);
	return;
}

void handle_zcpi(const char *s, const char *smsPdu)
{
   UNUSED(smsPdu);

   char    *line = NULL;
   char    *linesave = NULL;
   int err = -1;
   int callid = 0;
   int state = 0;

    line = strdup(s);
    linesave = line;
    err = at_tok_start(&line);
    if (err < 0)
    {
        goto error;
    }  
    err = at_tok_nextint(&line, &callid);
    if (err < 0)
    {
       goto error;
    }
	ALOGD("handle_zcpi : callid = %d",callid);
    err = at_tok_nextint(&line, &state);
    if (err < 0)
    {
        goto error;
    }  
	ALOGD("handle_zcpi : state = %d",state);
	if((5 == state)&&(1 == ring))
	{
	  ring = 0;
      RIL_onUnsolicitedResponse(RIL_UNSOL_RESPONSE_CALL_STATE_CHANGED, NULL, 0);
	}
    FREE(linesave);
    return;
error:
    FREE(linesave);
    ALOGE("%s: Error parameter in ind msg: %s", __FUNCTION__, s);
    return;
}

void handle_connect(const char *s, const char *smsPdu)
{
    UNUSED(s);
    UNUSED(smsPdu);

    // placeholder
}
void handle_cccm(const char *s, const char *smsPdu)
{
    UNUSED(s);
    UNUSED(smsPdu);

    // placeholder
}



