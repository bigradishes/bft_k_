/* //device/libs/telephony/ril_unsol_commands_ext.h
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
	{RIL_UNSOL_RESPONSE_EXT_BASE,responseVoid,WAKE_PARTIAL},
	//{RIL_UNSOL_SIM_ICCID, responseString, WAKE_PARTIAL},  
	//{RIL_UNSOL_PS_MOVING, responseInts, WAKE_PARTIAL}//delete by wangna for psmoving
	{RIL_UNSOL_GSM_SERVING_CELL_INFO_ZTE, responseString, WAKE_PARTIAL}

