/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>
#include <linux/in.h>
#include <bpf/bpf_endian.h>

// NAT is altering the source IP
#define SOURCE_IP 0xac166001 // 172.22.96.1
#define mip 0xac166564 //172.22.101.100 //my ip
#define TARGET_PORT 1111

struct
{
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __type(key, unsigned int);
    __type(value, unsigned int);
    __uint(max_entries, 64);
} xsks_map SEC(".maps");

// struct
// {
//     __uint(type, BPF_MAP_TYPE_ARRAY);
//     __type(key, unsigned int);
//     __type(value, unsigned long int);
//     __uint(max_entries, 2);
// } m SEC(".maps");
unsigned int key=0, v4;
unsigned long int v8, t0,td;
unsigned char v1;

SEC("xdp")
int xdp_sock_prog(struct xdp_md *ctx)
{
    t0 = (unsigned long int)bpf_ktime_get_ns();  // get the time when the packet is received

    int index = ctx->rx_queue_index;
    void *data_end = (void *)(long)ctx->data_end;
    void *data = (void *)(long)ctx->data;
    struct ethhdr *eth = data;
    struct iphdr *iph;
    struct udphdr *udph;

    // bpf_trace_printk("Packet received on queue %d\n", sizeof("Packet received on queue %d\n"), index);

    // packet data is at least the size of an Ethernet header
    if (data + sizeof(struct ethhdr) > data_end)
    {
        bpf_trace_printk("Packet size is smaller than Ethernet header\n", sizeof("Packet size is smaller than Ethernet header\n"));
        return XDP_PASS;
    }
    // Ethernet frame is an IP packet
    if (eth->h_proto != bpf_htons(ETH_P_IP))
    {
        bpf_trace_printk("Ethernet frame is not IP, protocol: %x\n", sizeof("Ethernet frame is not IP, protocol: %x\n"), bpf_ntohs(eth->h_proto));
        return XDP_PASS;
    }
    // packet data is at least the size of an IP header
    iph = data + sizeof(struct ethhdr);
    if ((void *)iph + sizeof(struct iphdr) > data_end)
    {
        bpf_trace_printk("Packet size is smaller than IP header\n", sizeof("Packet size is smaller than IP header\n"));
        return XDP_PASS;
    }
    // IP packet is a UDP packet
    if (iph->protocol != IPPROTO_UDP)
    {
        bpf_trace_printk("IP packet is not UDP, protocol: %x\n", sizeof("IP packet is not UDP, protocol: %x\n"), iph->protocol);
        return XDP_PASS;
    }

    // source IP address matches the target IP address
    // bpf_trace_printk("IP src: %x, expected: %x\n", sizeof("IP src: %x, expected: %x\n"), iph->saddr, bpf_ntohl(SOURCE_IP));

    if (iph->saddr != bpf_ntohl(SOURCE_IP))
        return XDP_PASS;
    // packet data is at least the size of a UDP header
    udph = (void *)iph + sizeof(struct iphdr);
    if ((void *)udph + sizeof(struct udphdr) > data_end)
    {
        bpf_trace_printk("Packet size is smaller than UDP header\n", sizeof("Packet size is smaller than UDP header\n"));
        return XDP_PASS;
    }
    if ((void *)udph + sizeof(struct udphdr)+1 > data_end)
    {
        bpf_trace_printk("packet has no extra data\n", sizeof("packet has no extra data\n"));
        return XDP_PASS;
    }

    // destination port matches the target port
    // bpf_trace_printk("UDP dest port: %d, expected: %d\n", sizeof("UDP dest port: %d, expected: %d\n"), bpf_ntohs(udph->dest), TARGET_PORT);
    if (udph->dest != bpf_htons(TARGET_PORT))
        return XDP_PASS;

    // packet is UDP, from the target IP, and to the target port, redirect it
    // bpf_trace_printk("Packet matches criteria, redirecting\n", sizeof("Packet matches criteria, redirecting\n"));

    //write packet (1 byte) to the map
    // key=1;
    // v1=*(char*)((void*)udph+8);
    // v8=v1;
    // bpf_map_update_elem(&m, &key, &v8, BPF_ANY);

    //swap source and destination IP addresses
    v4=iph->daddr;
    iph->daddr = iph->saddr;
    iph->saddr = v4;

    // swap source and destination MAC addresses
    for (int i = 0; i < 6; i++)
    {
        v1 = eth->h_dest[i];
        eth->h_dest[i] = eth->h_source[i];
        eth->h_source[i] = v1;
    }

    // packet is UDP, from the target IP, and to the target port, redirect it to userspace
    if (bpf_map_lookup_elem(&xsks_map, &index))
    {
        // bpf_trace_printk("Packet matches criteria, redirecting\n", sizeof("Packet matches criteria, redirecting\n")); 
        return bpf_redirect_map(&xsks_map, index, 0);
    }

    // key=0;
    // td = (unsigned long int)bpf_ktime_get_ns() - (unsigned long int)t0;
    // bpf_map_update_elem(&m, &key, &td, 0);
    
    return XDP_TX;
}

char _license[] SEC("license") = "GPL";