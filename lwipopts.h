#ifndef _LWIPOPTS_H
#define _LWIPOPTS_H

// Memory optimization for RP2040's 264KB RAM constraint
// This configuration minimizes lwIP memory usage while maintaining
// enough functionality for HTTP/HTTPS communication

// Operating mode - NO_SYS means no RTOS, poll mode only
#define NO_SYS                     1
#define LWIP_SOCKET                0    // Disable BSD socket API (use raw API)
#define LWIP_NETCONN               0    // Disable netconn API

// Memory alignment
#define MEM_ALIGNMENT              4

// Heap memory for lwIP dynamic allocations (8KB total)
#define MEM_SIZE                   8192

// Memory pools - tuned for minimal usage
#define MEMP_NUM_PBUF              16    // Packet buffers
#define MEMP_NUM_RAW_PCB           0     // No raw sockets needed
#define MEMP_NUM_UDP_PCB           2     // Minimal UDP (for DNS)
#define MEMP_NUM_TCP_PCB           5     // Max 5 concurrent TCP connections (was 3, increased for testing)
#define MEMP_NUM_TCP_PCB_LISTEN    2     // 2 listening sockets (kubelet + spare)
#define MEMP_NUM_TCP_SEG           16    // TCP segments
#define MEMP_NUM_NETBUF            0     // Not using netconn API
#define MEMP_NUM_NETCONN           0     // Not using netconn API

// Packet buffer pool
#define PBUF_POOL_SIZE             16    // Number of buffers in pool
#define PBUF_POOL_BUFSIZE          512   // Size of each buffer

// TCP Options
#define LWIP_TCP                   1
#define TCP_MSS                    1460  // Maximum Segment Size
#define TCP_SND_BUF                (4 * TCP_MSS)  // Send buffer: 5840 bytes
#define TCP_SND_QUEUELEN           ((4 * (TCP_SND_BUF) + (TCP_MSS - 1)) / (TCP_MSS))
#define TCP_WND                    (4 * TCP_MSS)  // Receive window
#define LWIP_TCP_KEEPALIVE         1     // Enable TCP keepalive

// TCP timers
#define TCP_TMR_INTERVAL           250   // TCP timer interval in ms

// UDP Options (needed for DNS)
#define LWIP_UDP                   1
#define UDP_TTL                    255

// ICMP Options (for ping)
#define LWIP_ICMP                  1

// DHCP Options
#define LWIP_DHCP                  1
#define DHCP_DOES_ARP_CHECK        0     // Skip ARP check to speed up DHCP

// DNS Options
#define LWIP_DNS                   1
#define DNS_TABLE_SIZE             2     // Small DNS cache
#define DNS_MAX_NAME_LENGTH        128

// Disable unnecessary features to save memory
#define LWIP_IGMP                  0     // No multicast
#define LWIP_AUTOIP                0     // No AutoIP
#define LWIP_SNMP                  0     // No SNMP
#define LWIP_PPP                   0     // No PPP

// IPv6 - Disable to save significant memory
#define LWIP_IPV6                  0

// Statistics and debugging - disable for production
#define LWIP_STATS                 0
#define LWIP_STATS_DISPLAY         0

// Checksum options - let hardware handle if available
#define CHECKSUM_GEN_IP            1
#define CHECKSUM_GEN_UDP           1
#define CHECKSUM_GEN_TCP           1
#define CHECKSUM_CHECK_IP          1
#define CHECKSUM_CHECK_UDP         1
#define CHECKSUM_CHECK_TCP         1

// Threading options - not needed in NO_SYS mode
#define LWIP_TCPIP_CORE_LOCKING    0

// Sequential layer options - disabled
#define LWIP_NETCONN               0
#define LWIP_SOCKET                0

// ARP options
#define LWIP_ARP                   1
#define ARP_TABLE_SIZE             10
#define ARP_QUEUEING               1

// IP options
#define IP_FORWARD                 0     // No IP forwarding
#define IP_REASSEMBLY              0     // No IP fragment reassembly (save memory)
#define IP_FRAG                    0     // No IP fragmentation

// Additional optimizations
#define LWIP_NETIF_HOSTNAME        1     // Enable setting hostname
#define LWIP_NETIF_STATUS_CALLBACK 1     // Enable status callbacks
#define LWIP_NETIF_LINK_CALLBACK   1     // Enable link callbacks

// Timeouts
#define LWIP_NETIF_LOOPBACK        0     // Disable loopback interface

#endif /* _LWIPOPTS_H */
