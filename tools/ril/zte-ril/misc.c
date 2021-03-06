/* //device/system/reference-ril/misc.c
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

/** returns 1 if line starts with prefix, 0 if it does not */
/* 如果开始返回1， 没有开始返回0 */
int strStartsWith(const char *line, const char *prefix)
{
    for (; *line != '\0' && *prefix != '\0'; line++, prefix++)
    {
        if (*line != *prefix)
        {
            return 0;
        }
    }

    return *prefix == '\0';
}

static int hexCharToInt(char c)
{
    if (c >= '0' && c <= '9')
    {
        return (c - '0');
    }
    if (c >= 'A' && c <= 'F')
    {
        return (c - 'A' + 10);
    }
    if (c >= 'a' && c <= 'f')
    {
        return (c - 'a' + 10);
    }
    else
    {
        return c;
    }
}

void convertHexStringToByte(const char *inData, int inLen, char *outData)
{
    int i;

    for (i = 0 ; i < inLen ; i += 2)
    {
        outData[i / 2] = (char) ((hexCharToInt(inData[i]) << 4) | hexCharToInt(inData[i + 1]));
    }
}


