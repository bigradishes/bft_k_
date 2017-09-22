/******************************************************************************
 * *(C) Copyright 2012 ZTE International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/

#ifndef ZTE_RIL_H_
#define ZTE_RIL_H_


#include <telephony/ril.h>
#include <stdlib.h>

#define LOG_TAG getLogTag()
#include <utils/Log.h>

#undef ALOG
#define ALOG(priority, tag, ...) \
    LOG_PRI(ANDROID_##priority, tag, __VA_ARGS__)

typedef RIL_CardStatus_v6   RIL_CardStatus;
typedef RIL_SIM_IO_v6   RIL_SIM_IO; 
typedef RIL_SignalStrength_v6   RIL_SignalStrength;

/*=====================================================================*/
//#define LIANTONG          //gelei  20160928
//#define DIANXIN          //gelei   20160928  only 4g
#define REQ_SIM_IO			//bufantong 20161207
#define TTY_USB_CHECK_AND_RESET    //bufantong 20160930
#define VER_TIME "201704011534"                    //VER_TIME "CUCC_201612161106" //"CTCC_2016112310" //gelei  20161027
//#define CHECK_SYSINFO              //gelei    20161129 //deleted by Boris
#define CHECK_SYSINFO_ONCE           //gelei  20161209
#define ALLWINNER_REF_MACHINE_METHOD      //gelei  20161209
//#define ZXW_RESET_METHOD   //bufantong 20161205 17.12  GELEI20161209
//#define RK_OTHERS_METHOD   //gelei  20161209
#define TTY_USB_CHECK_DEBUG  //gelei 20161209

#define CHECK_CREG_CEREG

#define V2    //bufantong 20170106 V2 ril 


//20161223 17.29
#define CMCC 1 //中国移动
#define CUCC 2 //中国联通
#define CTCC 3 //中国电信
#define CTT  4 //中国铁通



/*=====================================================================*/

//#define TSP_CP  
#define MUX_MODE 1
#define NORMAL_MODE 0

#define UART_TTY       "/dev/ttyO0"
#define SPI_TTY  "/dev/spitty0"

#define MAX_DATA_CALLS     4
#define MAX_AT_LENGTH     640   //512   //128

#define FREE(a)      if (a != NULL) { free(a); a = NULL; }
#define SETNULL(a)   if (a != NULL) { *a = NULL; }

#define UNUSED(a)    (void)(a)
/*for channel changing*/
#define RILHOOK_OEM_NAME_LENGTH      8            /* 8 bytes */
#define RILHOOK_OEM_REQUEST_ID_LEN   4            /* 4 bytes */
#define RILHOOK_OEM_REQUEST_DATA_LEN 4            /* 4 bytes */
#define RILHOOK_OEM_ITEMID_LEN       4            /* 4 bytes */
#define RILHOOK_OEM_ITEMID_DATA_LEN  4            /* 4 bytes */
#define BOARD_NUM_LEN                20           /* °?o?20 bytes */
#define TESTINFO_LEN                 10           /* 2aê?±ê????10 bytes */
#define GSM_RADIO_LEN                32           /* GSM?T??°?±?o?32 bytes */
#define RIL_HOOK_OEM_NAME              "QUALCOMM"

#define RIL_REQUEST_REGISTRATION_STATE RIL_REQUEST_VOICE_REGISTRATION_STATE
#define RIL_REQUEST_GPRS_REGISTRATION_STATE RIL_REQUEST_DATA_REGISTRATION_STATE 
#define RIL_UNSOL_RESPONSE_NETWORK_STATE_CHANGED RIL_UNSOL_RESPONSE_VOICE_NETWORK_STATE_CHANGED
#define RIL_CardStatus RIL_CardStatus_v6
#define RIL_SIM_IO RIL_SIM_IO_v6

#define DO_RESPONSE_JDUGE \
    do{\
        if (response->success == 0 || response->p_intermediates == NULL) goto error;\
    }while(0)
#define DO_RESPONSE_ERROR_JDUGE \
    do{\
        if (err < 0 || response->success == 0 || response->p_intermediates == NULL) goto error;\
    }while(0)    
#define DO_USIM_RESPONSE_JDUGE \
    do{\
        if (response->success == 0||strStartsWith(response->finalResponse, "+CME ERROR:") || response->p_intermediates == NULL)  goto error;\
    }while(0)
#define DO_ERROR_JDUGE \
    do{\
        if (err < 0)  goto error;\
    }while(0)
    
#define DO_MM_RESPONSE_ERROR_JDUGE \
	do{\
		if (err < 0 || response->success == 0) goto error;\
	}while(0)

typedef enum
{
    RILHOOK_BASE = 0x80100,
    RILHOOK_SET_AUDIOPATH,
    RILHOOK_SET_AUDIOVOLUME,
    RILHOOK_SET_RECORDCMD,
    RILHOOK_SET_MODEMOFF,
    RILHOOK_PLAY_TONE_CMD,
    RILHOOK_MAX
} rilhook_request_enum_type;
#ifdef RIL_SHLIB
const struct RIL_Env    *s_rilenv;

#define RIL_onRequestComplete(t, e, response, responselen) s_rilenv->OnRequestComplete(t, e, response, responselen)
#define RIL_onUnsolicitedResponse(a, b, c) s_rilenv->OnUnsolicitedResponse(a, b, c)
#define RIL_requestTimedCallback(a, b, c) s_rilenv->RequestTimedCallback(a, b, c)
#endif
typedef enum
{
    SIM_ABSENT = 0,
    SIM_NOT_READY = 1,
    SIM_READY = 2,
    /* SIM_READY means the radio state is RADIO_STATE_SIM_READY */
    SIM_PIN = 3,
    SIM_PUK = 4,
    SIM_NETWORK_PERSONALIZATION = 5,
    SIM_PIN2 = 6,
    SIM_PUK2 = 7,
    SIM_NETWORK_SUBSET_PERSONALIZATION = 8,
    SIM_SERVICE_PROVIDER_PERSONALIZATION = 9,
    SIM_CORPORATE_PERSONALIZATION = 10,
    SIM_SIM_PERSONALIZATION = 11,
    SIM_NETWORK_PERSONALIZATION_PUK = 12,
    SIM_NETWORK_SUBSET_PERSONALIZATION_PUK = 13,
    SIM_SERVICE_PROVIDER_PERSONALIZATION_PUK = 14,
    SIM_CORPORATE_PERSONALIZATION_PUK = 15,
    SIM_SIM_PERSONALIZATION_PUK = 16
} SIM_Status;

//#ifdef TSP_CP
typedef enum
{
    SERVICE_UNSOL = 0,
    SERVICE_PS,
    SERVICE_VT,
    SERVICE_CC,
    SERVICE_MM,
    SERVICE_MSG,
    SERVICE_SIM,
    SERVICE_DEV_SS,
    SERVICE_TOTAL
} ServiceType;
#define SERVICE_DEV   SERVICE_DEV_SS
#define SERVICE_SS    SERVICE_DEV_SS
#define SERVICE_NULL  SERVICE_TOTAL
//#endif


/*
enum channel_state {
    CHANNEL_IDLE,
    CHANNEL_BUSY,
};
*/
struct channel_description
{
    //int channelID;
    //int fd;
    const char  *ttyName;
    const char  *workQueueName;
    struct WorkQueue    *workQueue;
    //enum channel_state state;
    //pthread_mutex_t mutex;
};


struct psd_channel_decription
{
    int channelID;
    int fd;
    char    ttyName[128];
    char    ttyLock[128];
};

/**
 * @threadname: 线程名字
 * @write_channel: 写通道
 * @channel_timeout_msec: 通道超时时间，毫秒
 */
typedef struct ThreadContext_s
{
    char    threadname[16];
    int write_channel;
    long long   channel_timeout_msec;
} ThreadContext;

typedef void (*request_handler_t) (int request, void *data, size_t datalen, RIL_Token token);
typedef void (*indication_handler_t) (const char *s, const char *smsPdu);

struct request_info
{
    int request;
    ServiceType service;
    request_handler_t   handler;
};

struct indication_info
{
    const char  *prefix;
    ServiceType service;
    indication_handler_t    handler;
};

struct service_info
{
    ServiceType service;
    request_handler_t   pre_process_request;
    request_handler_t   post_process_request;
};







/* I/F implemented in ril.cpp */
extern const char *requestToString(int request);


/*Extern  I/F implemented in zte-ril.c */
const char *getLogTag();
void setThreadContext(ThreadContext *context);
ThreadContext *getThreadContext();

struct WorkQueue *getWorkQueue(int service);
void ril_handle_cmd_default_response_timeout(const char *cmd, RIL_Token token, long long msec);
void ril_handle_cmd_sms_one_int_timeout(const char *cmd, const char *sms_pdu, const char *prefix, RIL_Token token, long long msec);
inline static void ril_handle_cmd_one_int(const char *cmd, const char *prefix, RIL_Token token)
{
    ril_handle_cmd_sms_one_int_timeout(cmd, NULL, prefix, token, 0);
}

inline static void ril_handle_cmd_default_response(const char *cmd, RIL_Token token)
{
    ril_handle_cmd_default_response_timeout(cmd, token, 0);
}

typedef enum
{
    TSP_CP,
    CP,
    BROWNSTONE,
    WUKONG
}Modem_Type;

typedef enum
{
    POWER_OFF = 0,
    POWER_ON,
    POWER_RESET
}CP_Power_State;


extern int  modem_mode;
extern Modem_Type   modemType;
extern struct channel_description   *descriptions;
extern int  *service_channel_map;
extern struct psd_channel_decription    ppp_channel;

extern int get_channel_number(void);

typedef enum
{
    CHANNEL_00,
    CHANNEL_01,
    CHANNEL_02,
    CHANNEL_03,
    CHANNEL_04,
    CHANNEL_05,
    CHANNEL_06,
    CHANNEL_07,
    CHANNEL_08,
    CHANNEL_09,
    CHANNEL_10,
    CHANNEL_11,
    CHANNEL_12,
    CHANNEL_13,
    CHANNEL_14,
    CHANNEL_15,
    CHANNEL_16,
    CHANNEL_17,
    CHANNEL_18,
    CHANNEL_19,
    CHANNEL_20,
    CHANNEL_21,
    CHANNEL_22,
    CHANNEL_23,
    CHANNEL_24,
    CHANNEL_25,
    CHANNEL_26,
    CHANNEL_27,
    CHANNEL_28,
    CHANNEL_29,
    CHANNEL_30,
    CHANNEL_31
} ChannelType;

#define CHANNEL_UNSOLICITED   service_channel_map[SERVICE_UNSOL]
#define CHANNEL_DATA   service_channel_map[SERVICE_PS_CMD]

void setRadioState(RIL_RadioState newState);
RIL_RadioState getRadioState();
int needContinueProcess(int request, void *data, size_t datalen, RIL_Token token);


// in dataapi.c
void disableAllMobileInterfaces();

int isRadioOn(void);
//int isRegistered(void);
void resetLocalRegInfo(void);
void updateLocalRegInfo(void *param);
void enque_initializeCallback();

int max_support_data_call_count(void);

// AT*BAND related definition
//
// mode definition
#define MODE_GSM_ONLY 0
#define MODE_UMTS_ONLY 1
#define MODE_DUAL_MODE_AUTO 2
#define MODE_DUAL_MODE_GSM_PREFERRED 3
#define MODE_DUAL_MODE_UMTS_PREFERRED 4

//GSM band bit definition
#define GSMBAND_PGSM_900    0x01
#define GSMBAND_DCS_GSM_1800  0x02
#define GSMBAND_PCS_GSM_1900  0x04
#define GSMBAND_EGSM_900   0x08
#define GSMBAND_GSM_450  0x10
#define GSMBAND_GSM_480 0x20
#define GSMBAND_GSM_850 0x40

//UMTS band bit
#define UMTSBAND_BAND_1 0x01  //IMT-2100
#define UMTSBAND_BAND_2 0x02  //PCS-1900
#define UMTSBAND_BAND_3 0x04  //DCS-1800
#define UMTSBAND_BAND_4 0x08  //AWS-1700
#define UMTSBAND_BAND_5 0x10  //CLR-850
#define UMTSBAND_BAND_6 0x20  //800Mhz
#define UMTSBAND_BAND_7 0x40  //IMT-E 2600
#define UMTSBAND_BAND_8 0x80  //GSM-900
#define UMTSBAND_BAND_9 0x100 //not used

/* I/F implemented in ril-xxx.c */
/* request handler */
extern void ril_request_get_sim_status(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_handle_sim_pin_puk(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_current_calls(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_dial(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_imsi(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_hangup(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_hangup_waiting_or_background(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_hangup_foreground_resume_background(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_switch_waiting_or_holding_and_active(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_conference(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_udub(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_last_call_fail_cause(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_signal_strength(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_voice_registration_state(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_data_registration_state(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_operator(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_radio_power(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_dtmf(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_send_sms(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_setup_data_call(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_sim_io(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_send_ussd(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_cancel_ussd(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_clir(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_clir(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_query_call_forward_status(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_call_forward(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_query_call_waiting(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_call_waiting(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_sms_acknowledge(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_imei(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_imeisv(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_answer(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_deactivate_data_call(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_query_facility_lock(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_change_barring_password(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_query_network_selection_mode(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_network_selection_automatic(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_network_selection_manual(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_query_available_networks(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_dtmf_start(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_dtmf_stop(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_baseband_version(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_separate_connection(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_mute(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_mute(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_query_clip(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_last_data_call_fail_cause(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_data_call_list(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_reset_radio(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_radio_power_hardreset(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_oem_hook_raw(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_oem_hook_strings(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_screen_state(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_supp_svc_notification(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_write_sms_to_sim(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_delete_sms_on_sim(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_band_mode(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_query_available_band_mode(int request, void *data, size_t datalen, RIL_Token token);
//extern void ril_handle_stk_cmd_single_line(int request, void *data, size_t datalen, RIL_Token token);
//extern void ril_handle_stk_cmd_no_result(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_handle_stk_cmd_tsp(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_explicit_call_transfer(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_preferred_network_type(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_preferred_network_type(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_neighboring_cell_ids(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_location_updates(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_gsm_get_broadcast_sms_config(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_gsm_set_broadcast_sms_config(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_gsm_sms_broadcast_activation(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_device_identity(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_smsc_address(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_smsc_address(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_report_sms_memory_status(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_report_stk_service_is_running(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_acknowledge_incoming_gsm_sms_with_pdu(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_stk_send_envelope_with_status(int request, void *data, size_t datalen, RIL_Token token);
#ifdef ANDROID5_1_AND_6_0_SUPPORTED
/*******gelei 20170317,20170401*************/
extern void ril_request_sim_open_channel_get_ziccid(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_allow_data(int request, void *data, size_t datalen, RIL_Token token);
/***************************************************/

/****************chenjun 20170504*******************/
extern void ril_request_start_lce(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_stop_lce(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_pull_lcedata(int request, void *data, size_t datalen, RIL_Token token);
/***************************************************/

#endif

/* zte extended request handler*/
#ifdef ZTE_RIL_EXTEND
extern void ril_request_set_acm(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_acm(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_amm(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_amm(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_cpuc(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_cpuc(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_fast_dormancy(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_select_band(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_band(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_network_selection_manual_ext(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_query_colp(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_clip(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_colp(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_cnap(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_cnap(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_query_colr(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_colr(int request, void *data, size_t datalen, RIL_Token token);
//add by ax for TSP 7510 Modem begin
extern void ril_request_set_ps_mode(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_ps_transfer(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_play_callwait_tone_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_preferred_plmn_list_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_preferred_plmn_list_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_zemci_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_play_dtmf_tone(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_shut_down_radio(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_syn_pdp_context(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_define_pdp_context(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_zps_stat(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_bglte_plmn(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_zflag(int request, void *data, size_t datalen, RIL_Token token);

//add by ax for TSP 7510 Modem end

#endif

/* pre- and post- process handler*/
extern void cc_pre_process_request(int request, void *data, size_t datalen, RIL_Token token);
extern void cc_post_process_request(int request, void *data, size_t datalen, RIL_Token token);

/* Indication handler */
extern void handle_csq(const char *s, const char *smsPdu);
extern void handle_creg(const char *s, const char *smsPdu);
extern void handle_mode(const char *s, const char *smsPdu);
extern void handle_eemginfonc(const char *s, const char *smsPdu);
extern void handle_eemumtsind(const char *s, const char *smsPdu);
extern void handle_eemumtsinterrat(const char *s, const char *smsPdu);
extern void handle_eemumtssvc(const char *s, const char *smsPdu);
extern void handle_eemginfobasic(const char *s, const char *smsPdu);
extern void handle_eemginfosvc(const char *s, const char *smsPdu);
extern void handle_eemginfops(const char *s, const char *smsPdu);
extern void handle_eemginbftm(const char *s, const char *smsPdu);
extern void handle_nitz(const char *s, const char *smsPdu);

/*xichunyan 2012-07-27 for change nitz to zmmi  begin */
extern void handle_zmmi(const char *s, const char *smsPdu);
/*xichunyan 2012-07-27 for change nitz to zmmi end */
extern void handle_msri(const char *s, const char *smsPdu);
extern void handle_copn(const char *s, const char *smsPdu);
extern void handle_call_state_changed(const char *s, const char *smsPdu);
extern void handle_connect(const char *s, const char *smsPdu);
extern void handle_cccm(const char *s, const char *smsPdu);
extern void handle_cgev(const char *s, const char *smsPdu);
extern void handle_zgipdns(const char *s, const char *smsPdu);

extern void handle_cmt(const char *s, const char *smsPdu);
extern void handle_cmti(const char *s, const char *smsPdu);
extern void handle_cds(const char *s, const char *smsPdu);
extern void handle_mmsg(const char *s, const char *smsPdu);
extern void handle_cbm(const char *s, const char *smsPdu);
//extern void handle_cpin(const char *s, const char *smsPdu);
//extern void handle_mpbk(const char *s, const char *smsPdu);
//extern void handle_mstk(const char *s, const char *smsPdu);
//extern void handle_euicc(const char *s, const char *smsPdu);
//extern void handle_refresh(const char *s, const char *smsPdu);
extern void handle_zurdy(const char *s, const char *smsPdu);
extern void handle_zpbic(const char *s, const char *smsPdu);
extern void handle_zufch(const char *s, const char *smsPdu);
extern void handle_cssi(const char *s, const char *smsPdu);
extern void handle_cssu(const char *s, const char *smsPdu);
extern void handle_cusd(const char *s, const char *smsPdu);
extern void handle_zemci(const char *s, const char *smsPdu);
extern void handle_zuslot(const char *s, const char *smsPdu);
extern void handle_ring(const char *s, const char *smsPdu);
extern void handle_zcpi(const char *s, const char *smsPdu);
extern void handle_zsrving(const char *s, const char *smsPdu);


#ifdef ZTE_RIL_EXTEND
extern void ril_request_engineer_mode_cmd_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_gsm_information_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_attach_gprs(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_detach_gprs(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_query_3gqos(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_3gqos(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_handle_cp_reset_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_handle_imei_cmd_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_handle_sn_cmd_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_handle_cal_cmd_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_handle_adjust_cmd_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_handle_radio_cmd_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_handle_sensor_cmd_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_do_sim_auth_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_do_usim_auth_zte(int request, void *data, size_t datalen, RIL_Token token);
//added  by liutao 20131203 for TSP 7510 modem and dual standby start	
extern void ril_request_get_record_num_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_read_icc_card_record_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void requestWriteIccCardRecord(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_cgactt_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_cgsms_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_deactivate_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_3gqos_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_query_3gqos_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_icc_phonebook_record_info(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_sn_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_sn_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_del_sn_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_net_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_net_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_set_tdband_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_get_tdband_zte(int request, void *data, size_t datalen, RIL_Token token);

//added  by liutao 20131203 for TSP 7510 modem and dual standby end	
//mod by zouxiaojie 2013119 for TSP 7510 modem and dual standby begin
extern void ril_request_get_pinpuk_retries_zte(int request, void *data, size_t datalen, RIL_Token token);
extern void ril_request_sim_sms_capability_zte(int request, void *data, size_t datalen, RIL_Token token);
extern RIL_AppType getAppType();
extern RIL_AppType getKeepAppType();

//mod by zouxiaojie 2013119 for TSP 7510 modem and dual standby end
extern void ril_request_audiorec_vt(int request, void *data, size_t datalen, RIL_Token token);
extern void handle_connect_csd(const char *s, const char *smsPdu);
//add by ax for ps transfer brgin
extern void handle_psstate(const char *s, const char *smsPdu);
//add by ax for ps transfer end

//added by zhangzhiliang, 20140425, for handling frequent unsolicited ZPSSTAT begin
extern int createPSStatThread();
//added by zhangzhiliang, 20140425, for handling frequent unsolicited ZPSSTAT end

extern void ril_request_set_cgatt(int request, void *data, size_t datalen, RIL_Token token);

#endif /* ZTE_RIL_EXTEND */

extern void setCPstandby(void);

extern void handle_cereg(const char *s, const char *smsPdu);//added by boris for FDD LTE telecom,20160810

#endif // ZTE_RIL_H_

