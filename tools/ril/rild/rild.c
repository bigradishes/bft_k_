/* //device/system/rild/rild.c
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

/* 用于生成rild的可执行文件 */

#include <stdio.h> 
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h> 
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
//
#include <telephony/ril.h>
#define LOG_TAG "RILD"
#include <utils/Log.h>
#include <cutils/process_name.h>
#include <cutils/properties.h>
#include <cutils/sockets.h>
#include <linux/capability.h>
#include <linux/prctl.h>

#include <private/android_filesystem_config.h>
#include "hardware/qemu_pipe.h"





#define LIB_PATH_PROPERTY   "rild.libpath"
#define LIB_ARGS_PROPERTY   "rild.libargs"
#define MAX_LIB_ARGS        16

//#define NEW_TURNON_COM 1





#undef ALOG
#define ALOG(priority, tag, ...) \
    LOG_PRI(ANDROID_##priority, tag, __VA_ARGS__)


static void usage(const char *argv0)
{
    fprintf(stderr, "Usage: %s -l <ril impl library> [-- <args for impl library>]\n", argv0);
    exit(-1);
}

extern void RIL_register (const RIL_RadioFunctions *callbacks);

extern void RIL_onRequestComplete(RIL_Token t, RIL_Errno e,
                           void *response, size_t responselen);

extern void RIL_onUnsolicitedResponse(int unsolResponse, const void *data,
                                size_t datalen);

extern void RIL_requestTimedCallback (RIL_TimedCallback callback,
                               void *param, const struct timeval *relativeTime);


/* 这里面的三个函数是在ril.cpp里面实现，所以上面用extern声明 ，
 * 这个全局变量会传给ril_init函数，供下层调用
 */
static struct RIL_Env s_rilEnv = {
    RIL_onRequestComplete,
    RIL_onUnsolicitedResponse,
    RIL_requestTimedCallback
};

extern void RIL_startEventLoop();

static int make_argv(char * args, char ** argv)
{
    // Note: reserve argv[0]
    int count = 1;
    char * tok;
    char * s = args;

    while ((tok = strtok(s, " \0"))) {
        argv[count] = tok;
        s = NULL;
        count++;
    }
    return count;
}

/*
 * switchUser - Switches UID to radio, preserving CAP_NET_ADMIN capabilities.
 * Our group, cache, was set by init.
 */
/* 切换用户 */
void switchUser() {
/* 这个系统调用指令是为进程制定而设计的,它要求系统让它保持其功能 */
    prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0);
/* 设置实际用户ID和有效用户ID */
    setuid(AID_RADIO);

    struct __user_cap_header_struct header;
    struct __user_cap_data_struct cap;
    header.version = _LINUX_CAPABILITY_VERSION;
    header.pid = 0;
    cap.effective = cap.permitted = (1 << CAP_NET_ADMIN) | (1 << CAP_NET_RAW);
    cap.inheritable = 0;
	/* 设置进程权限 */
    capset(&header, &cap);
}



//system/bin/rild -l /system/lib/libril-v7r1.so
int main(int argc, char **argv)
{
    const char * rilLibPath = NULL;
    char **rilArgv;
    void *dlHandle;
    const RIL_RadioFunctions *(*rilInit)(const struct RIL_Env *, int, char **);
    const RIL_RadioFunctions *funcs;
    char libPath[PROPERTY_VALUE_MAX];
    unsigned char hasLibArgs = 0;

    int i;

	//设置文件没有哪些权限 
    umask(S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IWOTH | S_IXOTH);
	
    for (i = 1; i < argc ;) {
	ALOGD("Rild main argv[%d]: %s", i,argv[i]);
		/* if判断传入参数是否正确，然后把库的路径赋值 */
        if (0 == strcmp(argv[i], "-l") && (argc - i > 1)) {
			//得到库的路径
            rilLibPath = argv[i + 1];
            i += 2;
        } else if (0 == strcmp(argv[i], "--")) {
            i++;  //i=2
            hasLibArgs = 1;
            break;
        } else {
        	//打印
            usage(argv[0]);
        }
    }

	/**
	 * property_set：成功返回0，<0失败
	 * property_get：返回该值的长度,如果属性读取失败或返回一个空值
	 */
    if (rilLibPath == NULL) {
        if ( 0 == property_get(LIB_PATH_PROPERTY, libPath, NULL)) {
            // No lib sepcified on the command line, and nothing set in props.
            // Assume "no-ril" case.
            goto done;
        } else {
        	/* if内读取到属性，到这里赋值 */
            rilLibPath = libPath;
        }
    }

    /* special override when in the emulator */
#if 1
    {
        static char*  arg_overrides[3];
        static char   arg_device[32];
        int           done = 0;

//#define  REFERENCE_RIL_PATH  "/system/lib/libreference-ril.so"
#define  REFERENCE_RIL_PATH  "/system/lib/libril-v7r1.so"

        /* first, read /proc/cmdline into memory */
        char          buffer[1024], *p, *q;
        int           len;
						//读传递给内核的参数
        int           fd = open("/proc/cmdline",O_RDONLY);

        if (fd < 0) {
            ALOGD("could not open /proc/cmdline:%s", strerror(errno));
            goto OpenLib;
        }

        do {
            len = read(fd,buffer,sizeof(buffer)); }
        while (len == -1 && errno == EINTR);

        if (len < 0) {
            ALOGD("could not read /proc/cmdline:%s", strerror(errno));
            close(fd);
            goto OpenLib;
        }
        close(fd);

        if (strstr(buffer, "android.qemud=") != NULL)
        {
            /* the qemud daemon is launched after rild, so
            * give it some time to create its GSM socket
            */
            /* qemud守护进程是在rild之后启动的 
             * 给它一些时间来创建它的GSM socket
             */
            int  tries = 5;
#define  QEMUD_SOCKET_NAME    "qemud"

            while (1) {
				/* 什么作用? 没看懂 */
                int  fd;

                //sleep(1);   //zxj del for enter pin display delay

                fd = qemu_pipe_open("qemud:gsm");
                if (fd < 0) {
                    fd = socket_local_client(
                                QEMUD_SOCKET_NAME,
                                ANDROID_SOCKET_NAMESPACE_RESERVED,
                                SOCK_STREAM );
                }
                if (fd >= 0) {
                    close(fd);
                    snprintf( arg_device, sizeof(arg_device), "%s/%s",
                                ANDROID_SOCKET_DIR, QEMUD_SOCKET_NAME );

                    arg_overrides[1] = "-s";
                    arg_overrides[2] = arg_device;
                    done = 1;
                    break;
                }
                ALOGD("could not connect to %s socket: %s",
                    QEMUD_SOCKET_NAME, strerror(errno));
                if (--tries == 0)
                    break;
            }
            if (!done) {
                ALOGE("could not connect to %s socket (giving up): %s",
                    QEMUD_SOCKET_NAME, strerror(errno));
                while(1)
                    sleep(0x00ffffff);
            }
        }

        /* otherwise, try to see if we passed a device name from the kernel */
		/* 上面的if没有执行成功，就执行下面的这个if
		 * 大概理解是从传入内核参数里获取库的路径
		 */
        if (!done) do {
#define  KERNEL_OPTION  "android.ril="
#define  DEV_PREFIX     "/dev/"

            p = strstr( buffer, KERNEL_OPTION );
            if (p == NULL)
                break;

            p += sizeof(KERNEL_OPTION)-1;
            q  = strpbrk( p, " \t\n\r" );
            if (q != NULL)
                *q = 0;

            snprintf( arg_device, sizeof(arg_device), DEV_PREFIX "%s", p );
            arg_device[sizeof(arg_device)-1] = 0;
            arg_overrides[1] = "-d";
            arg_overrides[2] = arg_device;
            done = 1;

        } while (0);

        if (done) {
            argv = arg_overrides;
            argc = 3;
            i    = 1;
            hasLibArgs = 1;
            rilLibPath = REFERENCE_RIL_PATH;

            ALOGD("overriding with %s %s", arg_overrides[1], arg_overrides[2]);
        }
    }
OpenLib:
#endif
	/* 设置权限，切换用户 */
    switchUser();
    system("busybox killall -9 pppd");
	{ 
		char zterild_startflag[PROPERTY_VALUE_MAX];
		char startflagName[20] = {0};
		/*
		if(strstr(rilLibPath,"libzte-ril-ps")!= 0)
		{
		      ALOGD("RILD libzte-ril-ps \n");
			  set_process_name("rild-ps");
			  memcpy((char *)&startflagName, "ril.startflag1", strlen("ril.startflag1"));
		}
		else 
		{*/
		      ALOGD("RILD libzte-ril \n");
			  set_process_name("rild");
			  memcpy((char *)&startflagName, "ril.startflag", strlen("ril.startflag"));
		//}
/* delete by RenYimin_2015-11-12 for AP and CP communication BEGIN */
#if 0

        for (;; )
        {
            if ( 0 == property_get((char *)&startflagName, zterild_startflag, "0")) 
            {
                ALOGD("RILD get zterild_startflag property failed \n");
				return 0;
            } 

            if(0 == strcmp(zterild_startflag,"1"))
            {       
                break;
            }
			else
			{
				ALOGD("RILD get zterild_startflag property %s; waiting!\n",zterild_startflag);
            			sleep(1);
			}
			
        }
#endif
/* delete by RenYimin_2015-11-12 for AP and CP communication END */

    }
	//sleep(2);   //zxj del for enter pin display delay

	ALOGD("loading %s \n",rilLibPath);

	//打开一个动态链接库,并返回动态链接库的句柄 "/system/lib/libril-v7r1.so"
    dlHandle = dlopen(rilLibPath, RTLD_NOW);

    if (dlHandle == NULL) {
        ALOGE("dlopen failed: %s", dlerror());
        exit(-1);
    }

	//这就开始了 1.0
    RIL_startEventLoop();

    rilInit = (const RIL_RadioFunctions *(*)(const struct RIL_Env *, int, char **))dlsym(dlHandle, "RIL_Init");

    if (rilInit == NULL) {
        ALOGE("RIL_Init not defined or exported in %s\n", rilLibPath);
        exit(-1);
    }

    if (hasLibArgs) {
        rilArgv = argv + i - 1;
        argc = argc -i + 1;
    } else {
        static char * newArgv[MAX_LIB_ARGS];
        static char args[PROPERTY_VALUE_MAX];
        rilArgv = newArgv;
        property_get(LIB_ARGS_PROPERTY, args, "");
        argc = make_argv(args, rilArgv);
    }

    // Make sure there's a reasonable argv[0]
    rilArgv[0] = argv[0];
    //ALOGD("Rild main rilArgv: %s", rilArgv[0]);
	/* s_rilEnv中的成员是ril.cpp实现的，然后在rild.c中用extern声明的
	 * ，然后初始化的s_rilEnv结构体 
	 */
	/* 这里面的三个函数是在ril.cpp里面实现，所以上面用extern声明 ，
 	 * 这个全局变量会传给ril_init函数，供下层调用
 	 */
    funcs = rilInit(&s_rilEnv, argc, rilArgv);

    RIL_register(funcs);

done:

    while(1) {
        // sleep(UINT32_MAX) seems to return immediately on bionic
        sleep(0x00ffffff);
    }
}

