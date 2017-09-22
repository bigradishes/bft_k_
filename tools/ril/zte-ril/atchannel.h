/* //device/system/reference-ril/atchannel.h
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
 * ******************************************************************************/


#ifndef ATCHANNEL_H
#define ATCHANNEL_H 1

#ifdef __cplusplus
extern "C"
{
#endif

#define TIMEOUT_DEFALUT (30 * 1000)
#define TIMEOUT_CCFC (157 * 1000)
#define TIMEOUT_CCWA (157 * 1000)
#define TIMEOUT_CFUN (20 * 1000)
#define TIMEOUT_CGACT_DEACT (90 * 1000)
#define TIMEOUT_CGDATA (157 * 1000)
#define TIMEOUT_CHLD (14 * 1000)
#define TIMEOUT_CLCK_CALL_BARRING (42 * 1000)
#define TIMEOUT_CLCK (22 * 1000)
#define TIMEOUT_CLIR_GET (157 * 1000)
#define TIMEOUT_CMGS (60 * 1000)
#define TIMEOUT_CMGD (42 * 1000)
#define TIMEOUT_CMGW (12 * 1000)
#define TIMEOUT_COPS (180 * 1000)
#define TIMEOUT_COPS_TEST (180 * 1000)
#define TIMEOUT_CPIN (20 * 1000)
#define TIMEOUT_CPWD (22 * 1000)
#define TIMEOUT_CRSM (22 * 1000)
#define TIMEOUT_EBAND (22 * 1000)
#define TIMEOUT_ZMGL (50 * 1000)
#define TIMEOUT_ZGACT (90*1000)
#define TIMEOUT_CGATT (77*1000)


#define AT_ERROR_GENERIC -1
#define AT_ERROR_COMMAND_PENDING -2
#define AT_ERROR_CHANNEL_CLOSED -3
#define AT_ERROR_TIMEOUT -4
    /* AT commands may not be issued from
       reader thread (or unsolicited response
       callback */
#define AT_ERROR_INVALID_THREAD -5
    /* eg an at_send_command_singleline that
       did not get back an intermediate
       response */
#define AT_ERROR_INVALID_RESPONSE -6

    void init_all_channel_struct();

    typedef enum
    {
        NO_RESULT,
        /* no intermediate response expected */
        NUMERIC,
        /* a single intermediate response starting with a 0-9 */
        SINGLELINE,
        /* a single intermediate response starting with a prefix */
        MULTILINE,
        /* multiple line intermediate response starting with a prefix */
        MULTILINE_SMS /*xichunyan 2012-07-16 add multiple line intermediate response no need starting with a prefix */
    } ATCommandType;

    /** a singly-lined list of intermediate responses */
    typedef struct ATLine
    {
        struct ATLine   *p_next;
        char    *line;
    } ATLine;

    /** Free this with at_response_free() */
    typedef struct
    {
        int success;          /* true if final response indicates
                              success (eg "OK") */
        char    *finalResponse;            /* eg OK, ERROR */
        ATLine  *p_intermediates;       /* any intermediate responses */
    } ATResponse;

    /**
     * a user-provided unsolicited response handler function
     * this will be called from the reader thread, so do not block
     * "s" is the line, and "sms_pdu" is either NULL or the PDU response
     * for multi-line TS 27.005 SMS PDU responses (eg +CMT:)
     */
    typedef void (*ATUnsolHandler)(const char *s, const char *sms_pdu);

    int at_channel_init(ATUnsolHandler h1);
    int at_channel_open(int channelID, int fd);
    void at_channel_close(int channelID);

    /* This callback is invoked on the command thread.
       You should reset or handshake here to avoid getting out of sync */
    void at_set_on_timeout(void (*onTimeout) (int channelID));
    /* This callback is invoked on the reader thread (like ATUnsolHandler)
       when the input stream closes before you call at_close
       (not when you call at_close())
       You should still call at_close()
       It may also be invoked immediately from the current thread if the read
       channel is already closed */
    void at_set_on_reader_closed(void (*onClose) (void));


    void at_response_free(ATResponse *p_response);


    int at_send_command_full_ps(int cid, const char *command, ATCommandType type, const char *responsePrefix, const char *smspdu,
                                long long timeoutMsec, ATResponse **pp_outResponse);
    int at_send_command_timeout_ps(int cid, const char *command, ATResponse **pp_outResponse, long long timeoutMsec);

    int at_send_command_full(const char *command, ATCommandType type, const char *responsePrefix, const char *smspdu,
                             long long timeoutMsec, ATResponse **pp_outResponse);

    int at_send_command_timeout(const char *command, ATResponse **pp_outResponse, long long timeoutMsec);
    int at_send_command_singleline_timeout(const char *command, const char *responsePrefix, ATResponse **pp_outResponse,
                                           long long timeoutMsec);
    int at_send_command_numeric_timeout(const char *command, ATResponse **pp_outResponse, long long timeoutMsec);
    int at_send_command_multiline_timeout(const char *command, const char *responsePrefix, ATResponse **pp_outResponse,
                                          long long timeoutMsec);
    int at_send_command_sms_timeout(const char *command, const char *pdu, const char *responsePrefix, ATResponse **pp_outResponse,
                                    long long timeoutMsec);
    /*xichunyan 2012-07-16 add MULTILINE_SMS for getting pud by ZMGR begin */
    int at_send_command_multiline_sms(const char *command, const char *responsePrefix, ATResponse **pp_outResponse,
                                      long long timeoutMsec);
    /*xichunyan 2012-07-16 add MULTILINE_SMS for getting pud by ZMGR end */

    static inline int at_send_command(const char *command, ATResponse **pp_outResponse)
    {
        return at_send_command_timeout(command, pp_outResponse, 0);
    }

    static inline int at_send_command_singleline(const char *command, const char *responsePrefix, ATResponse **pp_outResponse)
    {
        return at_send_command_singleline_timeout(command, responsePrefix, pp_outResponse, 0);
    }

    static inline int at_send_command_numeric(const char *command, ATResponse **pp_outResponse)
    {
        return at_send_command_numeric_timeout(command, pp_outResponse, 0);
    }
    static inline int at_send_command_multiline(const char *command, const char *responsePrefix, ATResponse **pp_outResponse)
    {
        return at_send_command_multiline_timeout(command, responsePrefix, pp_outResponse, 0);
    }

    static inline int at_send_command_sms(const char *command, const char *pdu, const char *responsePrefix,
                                          ATResponse **pp_outResponse)
    {
        return at_send_command_sms_timeout(command, pdu, responsePrefix, pp_outResponse, 0);
    }

    struct  psd_channel_decription;
    int at_switch_data_mode(struct psd_channel_decription *pd, const char *cmd, ATResponse **pp_outResponse);

    typedef enum
    {
        CME_ERROR_NON_CME = -1,
        CME_SUCCESS = 0,
        CME_SIM_NOT_INSERTED = 10,
        CME_SIM_PIN2_REQUIRED = 17,
        CME_SIM_PUK2_REQUIRED = 18,
        CME_NOT_FOUND = 22,
        CME_MEM_FAIL = 23,
        CME_SIM_UNKNOWN_ERROR = 100,
    } AT_CME_Error;

    AT_CME_Error at_get_cme_error(const ATResponse *p_response);

    typedef enum
    {
        CMS_ERROR_NON_CMS = -1,
        CMS_SUCCESS = 0,
        CMS_NW_TIMEOUT = 332,
        CMS_UNKNOWN_ERROR = 500,
    } AT_CMS_Error;

    AT_CMS_Error at_get_cms_error(const ATResponse *p_response);

#ifdef __cplusplus
}
#endif

#endif /*ATCHANNEL_H*/
