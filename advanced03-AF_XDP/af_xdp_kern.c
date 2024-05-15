/* SPDX-License-Identifier: GPL-2.0 */

#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <bpf/bpf_helpers.h>
#include <linux/in.h>
#include <bpf/bpf_endian.h>

// #include <linux/types.h>

#define TARGET_IP 0x907A894F // Replace with target IP in hex eg. 144.122.137.79
#define TARGET_PORT 12345    // Replace with target port

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

    // Debug: Packet received
    bpf_trace_printk("Packet received on queue %d\n", sizeof("Packet received on queue %d\n"), index);

    // Check if packet data is at least the size of an Ethernet header
    if (data + sizeof(struct ethhdr) > data_end) {
        bpf_trace_printk("Packet size is smaller than Ethernet header\n", sizeof("Packet size is smaller than Ethernet header\n"));
        return XDP_PASS;
    }

    // Check if the Ethernet frame is an IP packet
    if (eth->h_proto != bpf_htons(ETH_P_IP)) {
        bpf_trace_printk("Ethernet frame is not IP, protocol: %x\n", sizeof("Ethernet frame is not IP, protocol: %x\n"), bpf_ntohs(eth->h_proto));
        return XDP_PASS;
    }

    // Check if packet data is at least the size of an IP header
    iph = data + sizeof(struct ethhdr);
    if ((void *)iph + sizeof(struct iphdr) > data_end) {
        bpf_trace_printk("Packet size is smaller than IP header\n", sizeof("Packet size is smaller than IP header\n"));
        return XDP_PASS;
    }

    // Check if the IP packet is a UDP packet
    if (iph->protocol != IPPROTO_UDP) {
        bpf_trace_printk("IP packet is not UDP, protocol: %x\n", sizeof("IP packet is not UDP, protocol: %x\n"), iph->protocol);
        return XDP_PASS;
    }

    // Check if the source IP address matches the target IP address
    bpf_trace_printk("IP src: %x, expected: %x\n", sizeof("IP src: %x, expected: %x\n"), iph->saddr, bpf_htonl(TARGET_IP));
    if (iph->saddr != bpf_htonl(TARGET_IP))
        return XDP_PASS;

    // Check if packet data is at least the size of a UDP header
    udph = (void *)iph + sizeof(struct iphdr);
    if ((void *)udph + sizeof(struct udphdr) > data_end) {
        bpf_trace_printk("Packet size is smaller than UDP header\n", sizeof("Packet size is smaller than UDP header\n"));
        return XDP_PASS;
    }

    // Check if the destination port matches the target port
    bpf_trace_printk("UDP dest port: %d, expected: %d\n", sizeof("UDP dest port: %d, expected: %d\n"), bpf_ntohs(udph->dest), TARGET_PORT);
    if (udph->dest != bpf_htons(TARGET_PORT))
        return XDP_PASS;

    // If the packet is UDP, from the target IP, and to the target port, redirect it
    bpf_trace_printk("Packet matches criteria, redirecting\n", sizeof("Packet matches criteria, redirecting\n"));
    if (bpf_map_lookup_elem(&xsks_map, &index))
        return bpf_redirect_map(&xsks_map, index, 0);

    return XDP_PASS;
}

char _license[] SEC("license") = "GPL";
