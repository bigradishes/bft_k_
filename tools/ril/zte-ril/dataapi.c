/******************************************************************************
 * *(C) Copyright 2012 ZTE International Ltd.
 * * All Rights Reserved
 * ******************************************************************************/

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <fcntl.h>
#include <cutils/properties.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netutils/ifc.h>
#include <netdb.h>
#include <netinet/in6.h>
#include <sys/ioctl.h>

#include "atchannel.h"
#include "zte-ril.h"
#define DEV_NETCARD "eth"

#define CCINET_IOC_MAGIC 241
#define CCINET_IP_ENABLE  _IOW(CCINET_IOC_MAGIC, 1, int)
#define CCINET_IP_DISABLE  _IOW(CCINET_IOC_MAGIC, 2, int)
#define TIOPPPON _IOW('T', 208, int)
#define TIOPPPOFF _IOW('T', 209, int)

extern struct psd_channel_decription    ppp_channel;
extern RIL_Data_Call_Response_v6  gPdpContList[MAX_DATA_CALLS];

static int getInterfaceAddr6(const char *ifname, char *ipaddress);
static int  ethNetdevfd[MAX_DATA_CALLS] = {-1,-1,-1,-1};
static int  pppNetdevfd = 0;

/* 如果pppd进程存在就kill掉 */
// Kill PPPD if it exists
static void kill_pppd()
{
	int ret = -1;
	FILE *fp = fopen("/mnt/asec/ppp0.pid", "r");
	if (fp)
	{
		int pid = -1;
		fscanf(fp, "%d", &pid);
		if (pid != -1)
		{
			char	cmd[256] = {0};
			sprintf(cmd, "kill %d", pid);
			ret = system(cmd);
			ALOGD("exec cmd: %s and ret is: %d!", cmd, ret);
			sleep(3); //Wait pppd exit
		}
		fclose(fp);
	}
    ALOGD("kill ppp successfully!");
}

/* 得到ppp属性的值 */
int is_ppp_enabled()
{
    char    value[PROPERTY_VALUE_MAX];
    property_get("zte.ril.ppp.enabled", value, "0");
    return atoi(value);
}

int max_support_data_call_count(void)
{
    if (is_ppp_enabled())
    {
        return 1;
    }
    else
    {
        return MAX_DATA_CALLS;
    }
}






static void set_mtu(const char *ifname, int mtu)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
        ALOGE("%s: Couldn't create IP socket: %s", __FUNCTION__, strerror(errno));
        return;
    }

    struct ifreq ifr={0};
    strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
    ifr.ifr_mtu = mtu;
    if (ioctl(sock, SIOCSIFMTU, &ifr) < 0)
    {
        ALOGE("%s: Couldn't set interface MTU to %d: %s", __FUNCTION__, mtu, strerror(errno));
    }
    close(sock);
    return;
}



int configureInterface4(char *address, char *ifname)
{
    char    gw[20] ={0} ;
    int 	ret=0;
    char    proper_name[PROPERTY_KEY_MAX] ={ 0 };
    if (ifc_init() != 0)
    {
        ALOGE("failed to ifc_init: %s", strerror(errno));       
        return -1;
    }
    if (ifc_up(ifname))
    {
        ALOGE("failed to turn on interface: %s", strerror(errno));
        ifc_close();
        return -1;
    }
    if (ifc_set_addr(ifname, inet_addr(address)))
    {
        ALOGE("failed to set ipaddr: %s", strerror(errno));
        ifc_close();
        return -1;
    }
    if (ifc_set_prefixLength(ifname, 24))
    {
        ALOGE("failed to set prefix length: %s", strerror(errno));
        ifc_close();
        return -1;
    }
    ifc_close();
    //set_mtu(ifname, 1500);
    return 0;
}

static int configureInterface6(struct in6_addr addr, char *ifname)
{
    int ret = -1;
    ifc_init();

    if (ifc_up(ifname))
    {
        ALOGW("failed to turn on interface");
        goto exit;
    }
    int count; //Wait to get the gloabal address
    for (count = 20 ; count > 0; count--)
    {
        sleep(1);
        char    ipaddress[INET6_ADDRSTRLEN]={0};
        ret = getInterfaceAddr6(ifname, ipaddress);
        if (ret == 0)
        {
            goto exit;
        }
    }
	//20151107 majun delete for ifc_down后无法上网
    //ifc_down(ifname);
exit:
    ifc_close();
    return ret;
}

//#ifdef SUPPORT_IPV6
void upInterface(char *ifname)
{
    ifc_init();

	ALOGD("enter upInterface");
    if (ifc_up(ifname))
    {
        ALOGW("failed to turn on");
        goto exit;
    }
   
    //ifc_down(ifname);
exit:
    ifc_close();
}

int getInterfaceip6gw(char *ifname, char *address, char *gw)
{
    int ret = -1;
	int count; //Wait to get the gloabal address

    ifc_init();

	ALOGD("enter getInterfaceip6gw");
    if (ifc_up(ifname))
    {
        ALOGW("failed to turn on interface6");
        goto exit;
    }
    for (count = 100 ; count > 0; count--)
    {
        sleep(1);
        //char    ipaddress[INET6_ADDRSTRLEN]={0};
        ret = getInterfaceAddr6gw(ifname, address, gw);
        if (ret == 0)
        {
            goto exit;
        }
    }
    //20151107 majun delete for ifc_down后无法上网
    //ifc_down(ifname);
exit:
    ifc_close();
    return ret;


}

int getInterfaceAddr6gw(const char *ifname, char *ipaddress, char *gwaddr)
{
    int ret = -1;
    int plen;
	int scope;
	int dad_status;
	int if_idx;
	char  devname[20]={0};
    char  addr6p[8][5] ={0};
	char  addr6_str[INET6_ADDRSTRLEN + 1] = {0};

    FILE  *f = fopen("/proc/net/if_inet6", "r");

	if (f == NULL)
    {
        ALOGE("Cannot open file: /proc/net/if_inet6");
        return ret;
    }	
	
	ALOGD("enter getInterfaceAddr6gw");

    while (fscanf(f, "%4s%4s%4s%4s%4s%4s%4s%4s %08x %02x %02x %02x %20s\n", addr6p[0], addr6p[1], addr6p[2], addr6p[3], addr6p[4], addr6p[5], addr6p[6], addr6p[7], &if_idx, &plen, &scope, &dad_status, devname) != EOF)
    {
        if (strcmp(devname, ifname) != 0)
        {
        	continue;
        }
		
        sprintf(addr6_str, "%s:%s:%s:%s:%s:%s:%s:%s", addr6p[0], addr6p[1], addr6p[2], addr6p[3], addr6p[4], addr6p[5], addr6p[6], addr6p[7]);

		ALOGD("enter getInterfaceAddr6gw ip=%s", addr6_str);

		if ((scope & 0x00f0U) == 0)
        {
            ALOGD("%s Global inet6 addr: %s/%d", ifname, addr6p, plen);
			
			struct in6_addr addr = {0};
            if (inet_pton(AF_INET6, addr6_str, &addr) != 1)
            {
                ALOGE("inet_pton failed");
                goto exit1;
            }
            int size = INET6_ADDRSTRLEN + 1;
			if(inet_ntop(AF_INET6, addr.s6_addr, ipaddress, size) == NULL)
			{
                ALOGE("inet_ntop failed");
                goto exit1;
            }
			ALOGD("enter getInterfaceAddr6gw ipaddress=%s", ipaddress);

#if 0
			addr.s6_addr[15] ^= 0xFF;
            if (inet_ntop(AF_INET6, addr.s6_addr, gwaddr, size) == NULL)
            {
                ALOGE("inet_ntop failed !!");
                goto exit1;
            }
#else
			{
				FILE *fp = NULL;
				int  ret = -1;
				
				if ((fp = fopen("/data/slaac_gw.conf", "r")) == NULL)
				{
                	ALOGE("fopen slaac_gw.conf failed: %s \n", strerror(errno));
                	goto exit1;
				}
				
				ret = fread(gwaddr, INET6_ADDRSTRLEN, 1, fp);

				ALOGE("fread gw = %s ret = %d", gwaddr, ret);
				
				fclose(fp);
			}
#endif			
			ALOGD("enter getInterfaceAddr6gw gw_addr_str=%s", gwaddr);	
            ret = 0;
            break;
        }

    }
    exit1 : fclose(f);
    return ret;
}
//#endif


int configureInterface(char *ifname,char* address)
{
    int ret = -1;
    struct addrinfo hints ={0};
	struct addrinfo *addr_ai = NULL;

	//strcpy(ifname,"eth0");
	//ALOGE("bufantong 20170105 --%s--\n",ifname);
	
	if(address == NULL) return ret;
	ALOGE("configureInterface: %s:%s", ifname,address);

    hints.ai_family = AF_UNSPEC;  /* Allow IPv4 or IPv6 */
    hints.ai_flags = AI_NUMERICHOST;
    getaddrinfo(address, NULL, &hints, &addr_ai);
    if (addr_ai->ai_family == AF_INET6)
    {
        struct sockaddr_in6 *sock_addr = (struct sockaddr_in6   *) addr_ai->ai_addr;
        ret = configureInterface6(sock_addr->sin6_addr, ifname);
    }
    else if (addr_ai->ai_family == AF_INET)
    {
        ret = configureInterface4(address, ifname);
    }
    else
    {
        ALOGE("configureInterface: getaddrinfo returned un supported address family %d", addr_ai->ai_family);
        ret = -1;
    }
    freeaddrinfo(addr_ai);
    return ret;
}


void deconfigureInterface(char *ifname)
{
	
	char proper_name[32] = { '\0' };

	sprintf(proper_name, "net.%s.gw", ifname);
	property_set(proper_name, "");
	sprintf(proper_name, "net.%s.dns1", ifname);
	property_set(proper_name, "");
	sprintf(proper_name, "net.%s.dns2", ifname);
	property_set(proper_name, "");
	
	ifc_init(); 
    if (ifc_down(ifname))
    {
        ALOGW("failed to turn off interface,error:%s",strerror(errno));
    }
    ifc_close();
    return;
}



void enableInterface(int cid)
{

    if (ethNetdevfd[cid -1] <= 0)
    {
        ALOGE("open ethNetdevfd fail");
        return;
    }
    ioctl(ethNetdevfd[cid -1], CCINET_IP_ENABLE, cid - 1);

}

void disableEthInterface(int cid)
{
    char    ifname[32] = {0 };
    sprintf(ifname, "%s%d", DEV_NETCARD,cid - 1);
    if (ethNetdevfd[cid-1] <= 0)
    {
        return;
    }
	
    ioctl(ethNetdevfd[cid-1], CCINET_IP_DISABLE, cid - 1);
}


//Wait to get the IP address
int waitPPPIpAddress(char *ipaddress)
{
	int count = 20;
	int ret = -1;
	unsigned myaddr = 0;
	ifc_init();
	while (count > 0)
	{
		sleep(1);
		ifc_get_info("ppp0", &myaddr, NULL, NULL);
		if (myaddr)
		{
			ret = 0;
			sprintf(ipaddress, "%d.%d.%d.%d", myaddr & 0xFF, (myaddr >> 8) & 0xFF, (myaddr >> 16) & 0xFF, (myaddr >> 24) & 0xFF);
			ALOGD("ifc_get_info  ppp0 IP address is: %s!\n", ipaddress);
			break;
		}
		--count;
	}
	ifc_close();
	return ret;
}

int enablePPPInterface(int cid, const char *user, const char *passwd, char *ipaddress)
{
    int ret = -1;
    pid_t   pid = 0;
    char    *args[] = { "/system/bin/pppd", "/dev/gsmtty4",  NULL };
    int status = 0;
    if (cid < 1)
    {
        ALOGE("%s: invalid cid:%d\n", __FUNCTION__, cid);
        return -1;
    }
	kill_pppd(); // Kill PPPD if it exists

    pid = fork();
    if (pid < 0)
    {
        ALOGE("Couldn't fork pppd process");
    }
    else if (0 == pid)
    {
        setpgrp();
        setsid();
        pid = fork(); // ALOGD("refork!!!");
        if (0 == pid)
        {
            ret = execvp("/system/bin/pppd", args); //   ALOGD("will leave 1!!!");
            ALOGD("Launch: pppd: %d !\n", ret);
            exit(0);
        }
        waitpid(pid, &status, 0); // ALOGD("will leave 2!!!");
        exit(0);
    }
    else
    {
        waitpid(pid, &status, 0);
    }
	return waitPPPIpAddress(ipaddress);
}



/* 禁止ppp接口 */
void disablePPPInterface(int cid)
{
	kill_pppd();    
    if (pppNetdevfd <= 0)
    {
    	ALOGW("failed to close PPP Net Dev");
        return;
    }
    ioctl(pppNetdevfd, TIOPPPOFF, cid - 1);
    close(pppNetdevfd);
	pppNetdevfd = -1;
	
}


void disableAllMobileInterfaces()
{
    /* 禁止ppp */
    if (is_ppp_enabled())
    {
        disablePPPInterface(1);
    }
    else
    {
    	int cid = 1 ;
		for (cid = 1; cid <= MAX_DATA_CALLS; cid++)
		{
			/* 禁止eth */
			disableEthInterface(cid);
		}
    }
}

static int getInterfaceAddr4(const char *ifname, char *ipaddress)
{
    int ret = -1;
    unsigned myaddr = 0;
    ifc_init();
    ifc_get_info(ifname, &myaddr, NULL, NULL);
    if (myaddr)
    {
        ret = 0;
        sprintf(ipaddress, "%d.%d.%d.%d", myaddr & 0xFF, (myaddr >> 8) & 0xFF, (myaddr >> 16) & 0xFF, (myaddr >> 24) & 0xFF);
    }
    ifc_close();
	ALOGD("getInterfaceAddr4 %d,%s:%s !\n",ret,ifname,ipaddress);	
    return ret;
}

// get global ipv6 address
static int getInterfaceAddr6(const char *ifname, char *ipaddress)
{
    int ret = -1;
    char devname[20]={0};
    int plen, scope, dad_status, if_idx;
    char  addr6p[8][5] ={0};
    FILE  *f = fopen("/proc/net/if_inet6", "r");
    if (f == NULL)
    {
        ALOGE("Cannot open file: /proc/net/if_inet6");
        return ret;
    }
	
    while (fscanf(f, "%4s%4s%4s%4s%4s%4s%4s%4s %08x %02x %02x %02x %20s\n", addr6p[0], addr6p[1], addr6p[2], addr6p[3], addr6p[4], addr6p[5], addr6p[6], addr6p[7], &if_idx, &plen, &scope, &dad_status, devname) != EOF)
    {
        if (strcmp(devname, ifname) != 0)
        {
        	continue;
        }
		
        sprintf(ipaddress, "%s:%s:%s:%s:%s:%s:%s:%s", addr6p[0], addr6p[1], addr6p[2], addr6p[3], addr6p[4], addr6p[5], addr6p[6], addr6p[7]);

		ALOGD("enter getInterfaceAddr6 ip=%s", ipaddress);

        if ((scope & 0x00f0U) == 0)
        {
            ALOGV("%s Global inet6 addr: %s/%d", ifname, plen);
            struct in6_addr addr = {0};
            if (inet_pton(AF_INET6, ipaddress, &addr) != 1)
            {
                ALOGE("inet_pton failed");
                goto exit1;
            }
            addr.s6_addr[15] ^= 0xFF;
            char    gw_addr_str[INET6_ADDRSTRLEN + 1] = {0};
            if (inet_ntop(AF_INET6, addr.s6_addr, gw_addr_str, sizeof(gw_addr_str)) == NULL)
            {
                ALOGE("inet_ntop failed");
                goto exit1;
            }
            char  proper_name[PROPERTY_KEY_MAX]={0};
            snprintf(proper_name, sizeof(proper_name), "net.%s.gw", ifname);
            ret = property_set(proper_name, gw_addr_str);
			if(ret !=0)
			{
				ALOGD("getInterfaceAddr6 property_set %s:%s;ret:%d\n", proper_name, gw_addr_str, ret);
			} else
			{
			    ALOGD("getInterfaceAddr6 property_set %s:%s;ret==00\n", proper_name, gw_addr_str);
			}
            break;
        }

    }
    exit1 : fclose(f);
    return ret;
}

int getInterfaceAddr(int af, const char *ifname, char *ipaddress)
{
    switch (af)
    {
        case AF_INET:
        return getInterfaceAddr4(ifname, ipaddress);
        case AF_INET6:
        return getInterfaceAddr6(ifname, ipaddress);
        default:
        errno = EAFNOSUPPORT;
        return (-1);
    }
}


