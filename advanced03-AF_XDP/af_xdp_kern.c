/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>
#include <linux/in.h>
#include <bpf/bpf_endian.h>

// NAT is altering the source IP
#define SOURCE_IP 0xC0A87A61 // 192.168.122.97 0xc0a87a61
#define TARGET_PORT 12345

struct
{
    __uint(type, BPF_MAP_TYPE_XSKMAP);
    __type(key, __u32);
    __type(value, __u32);
    __uint(max_entries, 64);
} xsks_map SEC(".maps");

SEC("xdp")
int xdp_sock_prog(struct xdp_md *ctx)
{
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
        return XDP_PASS;
    }
    // bpf_trace_printk("Packet size is smaller than Ethernet header\n", sizeof("Packet size is smaller than Ethernet header\n"));

    // Ethernet frame is an IP packet
    if (eth->h_proto != bpf_htons(ETH_P_IP))
    {
        return XDP_PASS;
    }
    // bpf_trace_printk("Ethernet frame is not IP, protocol: %x\n", sizeof("Ethernet frame is not IP, protocol: %x\n"), bpf_ntohs(eth->h_proto));

    // packet data is at least the size of an IP header
    iph = data + sizeof(struct ethhdr);
    if ((void *)iph + sizeof(struct iphdr) > data_end)
    {
        return XDP_PASS;
    }
    // bpf_trace_printk("Packet size is smaller than IP header\n", sizeof("Packet size is smaller than IP header\n"));

    // IP packet is a UDP packet
    if (iph->protocol != IPPROTO_UDP)
    {
        return XDP_PASS;
    }
    // bpf_trace_printk("IP packet is not UDP, protocol: %x\n", sizeof("IP packet is not UDP, protocol: %x\n"), iph->protocol);

    // source IP address matches the target IP address
    // notice due to address translation IP address is given as 192.168.122.1
    if (iph->saddr != bpf_ntohl(SOURCE_IP))
    {
        return XDP_PASS;
    }
    // bpf_trace_printk("IP src: %x, expected: %x\n", sizeof("IP src: %x, expected: %x\n"), iph->saddr, bpf_ntohl(SOURCE_IP));

    // packet data is at least the size of a UDP header
    udph = (void *)iph + sizeof(struct iphdr);
    if ((void *)udph + sizeof(struct udphdr) > data_end)
    {
        return XDP_PASS;
    }
    // bpf_trace_printk("Packet size is smaller than UDP header\n", sizeof("Packet size is smaller than UDP header\n"));

    // destination port matches the target port
    if (udph->dest != bpf_htons(TARGET_PORT))
    {
        // bpf_trace_printk("UDP dest port: %d, expected: %d\n", sizeof("UDP dest port: %d, expected: %d\n"), bpf_ntohs(udph->dest), TARGET_PORT);
        return XDP_PASS;
    }

    // packet is UDP, from the target IP, and to the target port, redirect it
    if (bpf_map_lookup_elem(&xsks_map, &index))
    {
        // bpf_trace_printk("Packet matches criteria, redirecting\n", sizeof("Packet matches criteria, redirecting\n"));
        return bpf_redirect_map(&xsks_map, index, 0);
    }

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
