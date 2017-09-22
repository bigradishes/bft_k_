/* //device/libs/telephony/ril_commands_ext.h
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
	{RIL_REQUEST_EXT_BASE,dispatchVoid,responseVoid},
    {RIL_REQUEST_SET_PS_MODE, dispatchInts ,responseVoid},
    {RIL_REQUEST_SET_PS_TRANSFER, dispatchInts ,responseVoid},
//mod by zouxiaojie 2013119 for TSP 7510 modem and dual standby begin
	{RIL_REQUEST_GET_PINPUK_RETRIES, dispatchVoid, responseInts},   
    {RIL_REQUEST_DO_SIM_AUTH, dispatchString, responseSimAuth},
    {RIL_REQUEST_DO_USIM_AUTH, dispatchStrings, responseUsimAuth},
    {RIL_REQUEST_SIM_SMS_CAPABILITY, dispatchVoid, responseInts},

//mod by zouxiaojie 2013119 for TSP 7510 modem and dual standby end
//add by ax for TSP 7510 begin
    {RIL_REQUEST_PLAY_CALLWAIT_TONE_ZTE, dispatchString, responseVoid},
    {RIL_REQUEST_SET_PREFERRED_PLMN_LIST_ZTE, dispatchPreferredPlmn, responseVoid},
    {RIL_REQUEST_GET_PREFERRED_PLMN_LIST_ZTE, dispatchVoid, responsePreferredPlmnList},
	{RIL_REQUEST_GET_RECORD_NUM, dispatchVoid, responseInts},
	{RIL_REQUEST_GET_ICC_PHONEBOOK_RECORD_INFO, dispatchVoid, responseInts},
	{RIL_REQUEST_READ_ICC_CARD_RECORD, dispatchInts, responseReadIccCardRecord},
	//{RIL_REQUEST_WRITE_ICC_CARD_RECORD, dispatchStrings, responseVoid},
	{RIL_REQUEST_WRITE_ICC_CARD_RECORD, dispatchIccWrite, responseInts},	
	{RIL_REQUEST_SET_CGATT, dispatchStrings, responseVoid},
	{RIL_REQUEST_SET_CGSMS,  dispatchStrings, responseVoid},
	{RIL_REQUEST_DEACTIVATE_PDP_ZTE, dispatchInts, responseVoid},
	{RIL_REQUEST_SET_3GQOS_ZTE, dispatchStrings, responseVoid},
	{RIL_REQUEST_QUERY_3GQOS_ZTE, dispatchVoid, responseInts},
	{RIL_REQUEST_GET_SN_ZTE, dispatchVoid, responseString},
	{RIL_REQUEST_SET_NET_ZTE, dispatchStrings, responseVoid},
	{RIL_REQUEST_GET_NET_ZTE, dispatchStrings, responseString},
	{RIL_REQUEST_SET_TDBAND_ZTE, dispatchInts, responseVoid},
	{RIL_REQUEST_GET_TDBAND_ZTE, dispatchVoid, responseString},
    {RIL_REQUEST_SET_MODEM_RESET, dispatchInts, responseVoid},
    {RIL_REQUEST_SET_IMEI_ZTE, dispatchStrings, responseVoid},
    {RIL_REQUEST_SET_SN_ZTE, dispatchStrings, responseVoid},
    {RIL_REQUEST_DEL_IMEI_ZTE,dispatchVoid, responseVoid},
    {RIL_REQUEST_DEL_SN_ZTE,  dispatchVoid, responseVoid},
    {RIL_REQUEST_DEL_CAL_ZTE,  dispatchInts, responseVoid},
    {RIL_REQUEST_SET_CAL_ZTE,  dispatchInts, responseVoid},
    {RIL_REQUEST_GET_CAL_ZTE,  dispatchInts, responseVoid},
    {RIL_REQUEST_GET_ADJUST_ZTE, dispatchVoid, responseString},
    {RIL_REQUEST_SET_CFUN_ZTE, dispatchInts, responseVoid},
    {RIL_REQUEST_DEL_SENSOR_CAL_ZTE,  dispatchVoid, responseVoid},
    {RIL_REQUEST_SET_SENSOR_CAL_ZTE,  dispatchVoid, responseVoid},
    {RIL_REQUEST_QUERY_PROMODE_ZTE,  dispatchVoid, responseString},
    {RIL_REQUEST_GET_LCD_FLAG,  dispatchVoid, responseString},
	{RIL_REQUEST_SET_LCD_FLAG,  dispatchString, responseVoid},
    {RIL_REQUEST_NEW_SMS_INDICATION, dispatchInts, responseVoid},
    {RIL_REQUEST_GET_SIM_TYPE, dispatchVoid, responseInts}, 
	{RIL_REQUEST_GET_MSISDN, dispatchVoid, responseString},	
	{RIL_REQUEST_ATTACH_GPRS_ZTE, dispatchStrings, responseVoid},
	{RIL_REQUEST_DETACH_GPRS_ZTE, dispatchStrings, responseVoid},
	{RIL_REQUEST_SET_GSM_INFO_PERIODIC_MODE_ZTE, dispatchVoid, responseVoid},
	{RIL_REQUEST_TURN_OFF_GSM_INFO_INDICATOR_ZTE, dispatchVoid, responseVoid},
	{RIL_REQUEST_PLAY_DTMF_TONE, dispatchString, responseVoid},
	{RIL_REQUEST_SHUT_DOWN_RADIO, dispatchVoid, responseVoid},
	{RIL_REQUEST_SYN_PDP_CONTEXT, dispatchInts ,responseVoid},
	{RIL_REQUEST_DEFINE_PDP_CONTEXT, dispatchStrings ,responseVoid},
	{RIL_REQUEST_GET_ZPS_STAT,dispatchVoid, responseInts},
	{RIL_REQUEST_GET_BGLTE_PLMN,dispatchVoid, responseStrings},
	{RIL_REQUEST_SET_ZFLAG, dispatchStrings ,responseVoid},

