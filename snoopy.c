//                                                       &oo{
//            _____ _____ _____ _____ ____ __   __    /~~~~~~~~\
//           |  ___|   | |  _  |  _  | __ |\ \ / /   /__________\
//           |___  | | | | |_| | |_| |  __| \   /      |______|
//        ---|_____|_|___|_____|_____|_|-----|_|-------|______|----
//      -------------------------------------------------------------
// Basic TCP/IP Sniffer for Windows, v1.2 by Antoni Sawicki <as@tenoware.com>
// Copyright (c) 2015-2016 by Antoni Sawicki
// Copyright (c) 2021 Google LLC
// Lincensed under BSD
//
// Note that this application can only snoop unicast TCP, UDP and ICMP traffic
// You cannot listen to layer 2, multicasts, broadcasts, etc. Also IPv4 only.
//
// todo:
// basic filtering options, for now use | findstr
// name resolution
// ipv6
//
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#define SIO_RCVALL _WSAIOW(IOC_VENDOR,1)
#define BUFFER_SIZE 65536
#define USAGE "\rUsage:\n\n%s [-v] <ipaddr>\n\nipaddr : local IP address on the NIC you want to attach to\n" \
"    -v : verbose mode, print more detailed protocol info\n\nv1.0 written by Antoni Sawicki <as@tenoware.com>\n"

char* proto[] = { "hopopt","ICMP","igmp","ggp","ipv4","st","TCP","cbt","egp","igp","bbn-rcc","nvp","pup","argus","emcon","xnet","chaos","UDP","mux","dcn","hmp","prm","xns-idp","trunk-1","trunk-2","leaf-1","leaf-2","rdp","irtp","iso-tp4","netblt","mfe-nsp","merit-inp","dccp","3pc","idpr","xtp","ddp","idpr-cmtp","tp++","il","ipv6","sdrp","ipv6-route","ipv6-frag","idrp","rsvp","gre","dsr","bna","esp","ah","i-nlsp","swipe","narp","mobile","tlsp","skip","ipv6-icmp","ipv6-nonxt","ipv6-opts","Unknown","cftp","Unknown","sat-expak","kryptolan","rvd","ippc","Unknown","sat-mon","visa","ipcv","cpnx","cphb","wsn","pvp","br-sat-mon","sun-nd","wb-mon","wb-expak","iso-ip","vmtp","secure-vmtp","vines","ttp","nsfnet-igp","dgp","tcf","eigrp","ospf","sprite-rpc","larp","mtp","ax.25","ipip","micp","scc-sp","etherip","encap","Unknown","gmtp","ifmp","pnni","pim","aris","scps","qnx","a/n","ipcomp","snp","compaq-peer","ipx-in-ip","vrrp","pgm","Unknown","l2tp","ddx","iatp","stp","srp","uti","smp","sm","ptp","isis","fire","crtp","crdup","sscopmce","iplt","sps","pipe","sctp","fc","rsvp-e2e-ignore","mobility-header","udplite","mpls-in-ip","manet","hip","shim6","wesp","rohc" };

typedef struct _IP_HEADER_ {
    BYTE  ip_hl : 4, ip_v : 4;
    BYTE  tos_dscp : 6, tos_ecn : 2;
    WORD  len;
    WORD  id;
    WORD  flags;
    BYTE  ttl;
    BYTE  protocol;
    WORD  chksum;
    DWORD src_ip;
    DWORD dst_ip;
} IPHEADER;

typedef struct _TCP_HEADER_ {
    WORD  source_port;
    WORD  destination_port;
    DWORD seq_number;
    DWORD ack_number;
    WORD  info_ctrl;
    WORD  window;
    WORD  checksum;
    WORD  urgent_pointer;
} TCPHEADER;

typedef struct _UDP_HEADER_ {
    WORD source_port;
    WORD destination_port;
    WORD len;
    WORD checksum;
} UDPHEADER;

typedef struct _ICMP_HEADER_ {
    BYTE type;
    BYTE code;
    WORD checksum;
} ICMPHEADER;

void errpt(char* msg, ...) {
    va_list valist;
    char errBuff[1024] = { 0 };
    DWORD err;

    printf("ERROR: ");
    va_start(valist, msg);
    vprintf(msg, valist);
    va_end(valist);
    err = WSAGetLastError();
    if (err) {
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), errBuff, sizeof(errBuff), NULL);
        printf("%d [%08X] %s\n", err, err, errBuff);
    }
    printf("\n");
    WSACleanup();
    ExitProcess(1);
}

IN_ADDR getIpAddr() {
    DWORD idx, status, size=0, i;
    PMIB_IPADDRTABLE iptbl;
    IN_ADDR ip;

    status=GetBestInterface(inet_addr("0.0.0.0"), &idx);
    if (status != NO_ERROR)
        errpt("GetBestInterface(): %d", status);

    iptbl = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MIB_IPADDRTABLE));
    if (iptbl == NULL)
        errpt("Unable to allocate memory for iptbl size");

    GetIpAddrTable(iptbl, &size, 0);
    HeapFree(GetProcessHeap(), 0, iptbl);
    iptbl = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (iptbl == NULL)
        errpt("Unable to allocate memory for IP Table");

    status = GetIpAddrTable(iptbl, &size, 0);
    if (status != NO_ERROR)
        errpt("GetIpAddrTable Err=%d", status);

    ip.S_un.S_addr = INADDR_NONE;
    for (i = 0; i < iptbl->dwNumEntries; i++) {
        if (iptbl->table[i].dwIndex == idx) {
            ip.S_un.S_addr = iptbl->table[i].dwAddr;
            HeapFree(GetProcessHeap(), 0, iptbl);
            return ip;
        }
    }
    errpt("No ip address specified and no suitable interface found");
    return ip;
}

int main(int argc, char** argv) {                       //                   .o.
    struct      sockaddr_in snoop_addr;                 //                   |  |    _   ,
    SOCKET      snoop_sock = -1;                        //                 .',  L.-'` `\ ||
    WSADATA     sa_data;                                //               __\___,|__--,__`_|__
    IPHEADER*   ip_header;                              //              |    %     `=`       |
    TCPHEADER*  tcp_header;                             //              | ___%_______________|
    UDPHEADER*  udp_header;                             //              |    `               |
    ICMPHEADER* icmp_header;                            //              | -------------------|
    BYTE        flags;                                  //              |____________________|
    DWORD       optval = 1, dwLen = 0, verbose = 0;     //                |~~~~~~~~~~~~~~~~|
    char*       packet;                                 //            jgs | ---------------|  ,
    IN_ADDR     bindIP;                                 //            \|  | _______________| / /
    IN_ADDR     pktIP;                                  //         \. \,\\|, .   .   /,  / |///, /
    char        src_ip[20], dst_ip[20];
    SYSTEMTIME  lt;

    /*if (argc >) {
        if ((argv[1][0] == '-' || argv[1][0] == '/') && argv[1][1] == 'v')
            verbose = 1;
        else if (isdigit(argv[1][0]))
            bindIP.S_un.S_addr = inet_addr(argv[1]);
        else
            errpt(USAGE);
    }
    else if (argc == 3) {
        if ((argv[1][0] == '-' || argv[1][0] == '/') && argv[1][1] == 'v')
            verbose = 1;

    }*/
    bindIP = getIpAddr();

    if (WSAStartup(MAKEWORD(2, 2), &sa_data)!=0)
        errpt("Starting WSA");

    snoop_sock = WSASocket(AF_INET, SOCK_RAW, IPPROTO_IP, NULL, 0, 0);
    if (snoop_sock == SOCKET_ERROR)
        errpt("Opening Socket");

    snoop_addr.sin_family = AF_INET;
    snoop_addr.sin_port = htons(0);
    snoop_addr.sin_addr = bindIP;
    if (snoop_addr.sin_addr.s_addr == INADDR_NONE)
        errpt("Incorrect IP address");

    printf("Binding to %s\n", inet_ntoa(snoop_addr.sin_addr));

    if (bind(snoop_sock, (struct sockaddr*)&snoop_addr, sizeof(snoop_addr)) == SOCKET_ERROR)
        errpt("Bind to %s", inet_ntoa(snoop_addr.sin_addr));

    if (WSAIoctl(snoop_sock, SIO_RCVALL, &optval, sizeof(optval), NULL, 0, &dwLen, NULL, NULL) == SOCKET_ERROR)
        errpt("SIO_RCVALL");

    packet = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, BUFFER_SIZE);
    if (packet == NULL)
        errpt("Unable to allocate memory");

    while (1) {
        ZeroMemory(packet, BUFFER_SIZE);
        ZeroMemory(src_ip, sizeof(src_ip));
        ZeroMemory(dst_ip, sizeof(dst_ip));
        ip_header = NULL;
        tcp_header = NULL;
        udp_header = NULL;
        icmp_header = NULL;

        if (recv(snoop_sock, packet, BUFFER_SIZE, 0) < sizeof(IPHEADER))
            continue;

        ip_header = (IPHEADER*)packet;

        if (ip_header->ip_v != 4)
            continue;

        pktIP.S_un.S_addr = ip_header->src_ip;
        strcpy(src_ip, inet_ntoa(pktIP));
        pktIP.S_un.S_addr = ip_header->dst_ip;
        strcpy(dst_ip, inet_ntoa(pktIP));

        GetLocalTime(&lt);

        // TCP
        if (ip_header->protocol == 6) {
            printf("%02d:%02d:%02d.%03d %s ", lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds, proto[ip_header->protocol]);
            tcp_header = (TCPHEADER*)&packet[ip_header->ip_hl * sizeof(DWORD)];
            flags = (ntohs(tcp_header->info_ctrl) & 0x003F);
            printf("%s:%ld -> %s:%ld ", src_ip, htons(tcp_header->source_port), dst_ip, htons(tcp_header->destination_port));
            if (flags & 0x01) printf("FIN ");
            if (flags & 0x02) printf("SYN ");
            if (flags & 0x04) printf("RST ");
            if (flags & 0x08) printf("PSH ");
            if (flags & 0x10) printf("ACK ");
            if (flags & 0x20) printf("URG ");
            if (verbose) printf("seq %lu ", ntohl(tcp_header->seq_number));
            if (verbose) printf("ack %lu ", ntohl(tcp_header->ack_number));
            if (verbose) printf("win %u ", ntohs(tcp_header->window));
        }

        // UDP
        else if (ip_header->protocol == 17) {
            printf("%02d:%02d:%02d.%03d %s ", lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds, proto[ip_header->protocol]);
            udp_header = (UDPHEADER*)&packet[ip_header->ip_hl * sizeof(DWORD)];
            printf("%s:%ld -> %s:%ld ", src_ip, htons(udp_header->source_port), dst_ip, htons(udp_header->destination_port));
        }

        // ICMP
        else if (ip_header->protocol == 1) {
            printf("%02d:%02d:%02d.%03d %s ", lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds, proto[ip_header->protocol]);
            icmp_header = (ICMPHEADER*)&packet[ip_header->ip_hl * sizeof(DWORD)];
            printf("%s -> %s ", src_ip, dst_ip);
            printf("type %d code %d ", icmp_header->type, icmp_header->code);
            if (icmp_header->type == 0) printf("[echo reply] ");
            else if (icmp_header->type == 8) printf("[echo request] ");
            else if (icmp_header->type == 3) printf("[dst unreachable] ");
            else if (icmp_header->type == 5) printf("[redirect] ");
            else if (icmp_header->type == 1) printf("[time exceeded] ");
        }

        else {
            printf("%02d:%02d:%02d.%03d %s ", lt.wHour, lt.wMinute, lt.wSecond, lt.wMilliseconds, proto[ip_header->protocol]);
            printf("%s -> %s ", src_ip, dst_ip);
        }

        if (verbose) printf("dscp %u ecn %u ttl %u ", ip_header->tos_dscp, ip_header->tos_ecn, ip_header->ttl);
        if (ntohs(ip_header->flags) & 0x4000) printf("DF ");
    end:
        putchar('\n');
        fflush(stdout); // helps findstr
    }
    return 0;
}
