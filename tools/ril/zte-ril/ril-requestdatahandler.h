/* ST-Ericsson U300 RIL
**
** Copyright (C) ST-Ericsson AB 2010
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
**
** Author: Christian Bejram <christian.bejram@stericsson.com>
*/
#ifndef _U300_RIL_REQUESTDATAHANDLER_H
#define _U300_RIL_REQUESTDATAHANDLER_H 1

typedef struct RILRequest
{
    int request;
    void    *data;
    size_t  datalen;
    RIL_Token   token;
} RILRequest;

RILRequest *newRILRequest(int request, void *data, size_t datalen, RIL_Token token);
void freeRILRequest(RILRequest *r);

//void *dupRequestData(int requestId, void *data, size_t datalen);
//void freeRequestData(int requestId, void *data, size_t datalen);

#endif
