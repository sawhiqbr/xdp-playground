/* SPDX-License-Identifier: GPL-2.0 */

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <locale.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/resource.h>

#include <bpf/bpf.h>
#include <xdp/xsk.h>
#include <xdp/libxdp.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <linux/if_link.h>
#include <linux/if_ether.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>

#include <linux/ip.h>
#include <linux/udp.h>

#include "../common/common_params.h"
#include "../common/common_user_bpf_xdp.h"
#include "../common/common_libbpf.h"

#define NUM_FRAMES 4096
#define FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define RX_BATCH_SIZE 64
#define INVALID_UMEM_FRAME UINT64_MAX

#define BUFFER_SIZE 1024

// UDP packet structure
struct udp_custom_packet
{
    char file_name[8];
    uint32_t sequence_number;
    uint32_t total_chunks;
    uint8_t message[BUFFER_SIZE - 15];
};

static struct xdp_program *prog;
int xsk_map_fd;
bool custom_xsk = false;
struct config cfg = {
    .ifindex = -1,
};

struct xsk_umem_info
{
    struct xsk_ring_prod fq;
    struct xsk_ring_cons cq;
    struct xsk_umem *umem;
    void *buffer;
};

struct xsk_socket_info
{
    struct xsk_ring_cons rx;
    struct xsk_ring_prod tx;
    struct xsk_umem_info *umem;
    struct xsk_socket *xsk;

    uint64_t umem_frame_addr[NUM_FRAMES];
    uint32_t umem_frame_free;

    uint32_t outstanding_tx;
};

static inline __u32 xsk_ring_prod__free(struct xsk_ring_prod *r)
{
    r->cached_cons = *r->consumer + r->size;
    return r->cached_cons - r->cached_prod;
}

static const char *__doc__ = "AF_XDP kernel bypass example\n";

static const struct option_wrapper long_options[] = {
    {{"help", no_argument, NULL, 'h'}, "Show help", false},
    {{"dev", required_argument, NULL, 'd'}, "Operate on device <ifname>", "<ifname>", true},
    {{"skb-mode", no_argument, NULL, 'S'}, "Install XDP program in SKB (AKA generic) mode"},
    {{"native-mode", no_argument, NULL, 'N'}, "Install XDP program in native mode"},
    {{"auto-mode", no_argument, NULL, 'A'}, "Auto-detect SKB or native mode"},
    {{"force", no_argument, NULL, 'F'}, "Force install, replacing existing program on interface"},
    {{"copy", no_argument, NULL, 'c'}, "Force copy mode"},
    {{"zero-copy", no_argument, NULL, 'z'}, "Force zero-copy mode"},
    {{"queue", required_argument, NULL, 'Q'}, "Configure interface receive queue for AF_XDP, default=0"},
    {{"poll-mode", no_argument, NULL, 'p'}, "Use the poll() API waiting for packets to arrive"},
    {{"quiet", no_argument, NULL, 'q'}, "Quiet mode (no output)"},
    {{"filename", required_argument, NULL, 1}, "Load program from <file>", "<file>"},
    {{"progname", required_argument, NULL, 2}, "Load program from function <name> in the ELF file", "<name>"},
    {{0, 0, NULL, 0}, NULL, false}};

static bool global_exit;

static struct xsk_umem_info *configure_xsk_umem(void *buffer, uint64_t size)
{
    struct xsk_umem_info *umem;
    int ret;

    umem = calloc(1, sizeof(*umem));
    if (!umem)
        return NULL;

    ret = xsk_umem__create(&umem->umem, buffer, size, &umem->fq, &umem->cq, NULL);
    if (ret)
    {
        errno = -ret;
        return NULL;
    }

    umem->buffer = buffer;
    return umem;
}

static uint64_t xsk_alloc_umem_frame(struct xsk_socket_info *xsk)
{
    uint64_t frame;
    if (xsk->umem_frame_free == 0)
        return INVALID_UMEM_FRAME;

    frame = xsk->umem_frame_addr[--xsk->umem_frame_free];
    xsk->umem_frame_addr[xsk->umem_frame_free] = INVALID_UMEM_FRAME;
    return frame;
}

static void xsk_free_umem_frame(struct xsk_socket_info *xsk, uint64_t frame)
{
    assert(xsk->umem_frame_free < NUM_FRAMES);

    xsk->umem_frame_addr[xsk->umem_frame_free++] = frame;
}

static uint64_t xsk_umem_free_frames(struct xsk_socket_info *xsk)
{
    return xsk->umem_frame_free;
}

static struct xsk_socket_info *xsk_configure_socket(struct config *cfg, struct xsk_umem_info *umem)
{
    struct xsk_socket_config xsk_cfg;
    struct xsk_socket_info *xsk_info;
    uint32_t idx;
    int i;
    int ret;
    uint32_t prog_id;

    xsk_info = calloc(1, sizeof(*xsk_info));
    if (!xsk_info)
        return NULL;

    xsk_info->umem = umem;
    xsk_cfg.rx_size = XSK_RING_CONS__DEFAULT_NUM_DESCS;
    xsk_cfg.tx_size = XSK_RING_PROD__DEFAULT_NUM_DESCS;
    xsk_cfg.xdp_flags = cfg->xdp_flags;
    xsk_cfg.bind_flags = cfg->xsk_bind_flags;
    xsk_cfg.libbpf_flags = (custom_xsk) ? XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD : 0;
    ret = xsk_socket__create(&xsk_info->xsk, cfg->ifname, cfg->xsk_if_queue, umem->umem, &xsk_info->rx, &xsk_info->tx, &xsk_cfg);
    if (ret)
        goto error_exit;

    if (custom_xsk)
    {
        ret = xsk_socket__update_xskmap(xsk_info->xsk, xsk_map_fd);
        if (ret)
            goto error_exit;
    }
    else
    {
        /* Getting the program ID must be after the xdp_socket__create() call */
        if (bpf_xdp_query_id(cfg->ifindex, cfg->xdp_flags, &prog_id))
            goto error_exit;
    }

    /* Initialize umem frame allocation */
    for (i = 0; i < NUM_FRAMES; i++)
        xsk_info->umem_frame_addr[i] = i * FRAME_SIZE;

    xsk_info->umem_frame_free = NUM_FRAMES;

    /* Stuff the receive path with buffers, we assume we have enough */
    ret = xsk_ring_prod__reserve(&xsk_info->umem->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS, &idx);

    if (ret != XSK_RING_PROD__DEFAULT_NUM_DESCS)
        goto error_exit;

    for (i = 0; i < XSK_RING_PROD__DEFAULT_NUM_DESCS; i++)
        *xsk_ring_prod__fill_addr(&xsk_info->umem->fq, idx++) = xsk_alloc_umem_frame(xsk_info);

    xsk_ring_prod__submit(&xsk_info->umem->fq, XSK_RING_PROD__DEFAULT_NUM_DESCS);

    return xsk_info;

error_exit:
    errno = -ret;
    return NULL;
}

static void complete_tx(struct xsk_socket_info *xsk)
{
    unsigned int completed;
    uint32_t idx_cq;

    if (!xsk->outstanding_tx)
        return;

    sendto(xsk_socket__fd(xsk->xsk), NULL, 0, MSG_DONTWAIT, NULL, 0);

    /* Collect/free completed TX buffers */
    completed = xsk_ring_cons__peek(&xsk->umem->cq, XSK_RING_CONS__DEFAULT_NUM_DESCS, &idx_cq);

    if (completed > 0)
    {
        for (int i = 0; i < completed; i++)
            xsk_free_umem_frame(xsk, *xsk_ring_cons__comp_addr(&xsk->umem->cq, idx_cq++));

        xsk_ring_cons__release(&xsk->umem->cq, completed);
        xsk->outstanding_tx -= completed < xsk->outstanding_tx ? completed : xsk->outstanding_tx;
    }
}

static inline __sum16 csum16_add(__sum16 csum, __be16 addend)
{
    uint16_t res = (uint16_t)csum;

    res += (__u16)addend;
    return (__sum16)(res + (res < (__u16)addend));
}

static inline __sum16 csum16_sub(__sum16 csum, __be16 addend)
{
    return csum16_add(csum, ~addend);
}

static inline void csum_replace2(__sum16 *sum, __be16 old, __be16 new)
{
    *sum = ~csum16_add(csum16_sub(~(*sum), old), new);
}

static bool process_packet(struct xsk_socket_info *xsk, uint64_t addr, uint32_t len)
{
    uint8_t *pkt = xsk_umem__get_data(xsk->umem->buffer, addr);

    struct ethhdr *eth = (struct ethhdr *)pkt;
    if (ntohs(eth->h_proto) != ETH_P_IP)
    {
        printf("Shouldn't have end up here: we arent ethernet\n");
        return false;
    }

    struct iphdr *iph = (struct iphdr *)(eth + 1);
    if (iph->protocol != IPPROTO_UDP)
    {
        printf("Shouldn't have end up here: we arent udp\n");
        return false;
    }

    struct udphdr *udph = (struct udphdr *)((uint8_t *)iph + sizeof(*iph));
    struct udp_custom_packet *cust_pkt = (struct udp_custom_packet *)(udph + 1);

    printf("Received UDP packet:\n");
    printf("File name: %s\n", cust_pkt->file_name);
    printf("Sequence number: %u\n", ntohl(cust_pkt->sequence_number));
    printf("Total chunks: %u\n", ntohl(cust_pkt->total_chunks));
    printf("Message: %.*s\n", len - sizeof(struct udphdr) - sizeof(struct udp_custom_packet) + sizeof(cust_pkt->message), cust_pkt->message);

    // uint8_t *payload = (uint8_t *)(udph + 1);
    // uint32_t payload_len = ntohs(udph->len) - sizeof(*udph);
    // printf("Received UDP payload: %.*s\n", payload_len, payload);

    // Process the packet further as needed
    // For now, we just free the frame
    xsk_free_umem_frame(xsk, addr);
    return true;
}

static void handle_receive_packets(struct xsk_socket_info *xsk)
{
    unsigned int rcvd, stock_frames, i;
    uint32_t idx_rx = 0, idx_fq = 0;
    int ret;

    rcvd = xsk_ring_cons__peek(&xsk->rx, RX_BATCH_SIZE, &idx_rx);
    if (!rcvd)
        return;

    /* Stuff the ring with as much frames as possible */
    stock_frames = xsk_prod_nb_free(&xsk->umem->fq, xsk_umem_free_frames(xsk));

    if (stock_frames > 0)
    {
        ret = xsk_ring_prod__reserve(&xsk->umem->fq, stock_frames, &idx_fq);

        /* This should not happen, but just in case */
        while (ret != stock_frames)
            ret = xsk_ring_prod__reserve(&xsk->umem->fq, rcvd, &idx_fq);

        for (i = 0; i < stock_frames; i++)
            *xsk_ring_prod__fill_addr(&xsk->umem->fq, idx_fq++) = xsk_alloc_umem_frame(xsk);

        xsk_ring_prod__submit(&xsk->umem->fq, stock_frames);
    }

    /* Process received packets */
    for (i = 0; i < rcvd; i++)
    {
        uint64_t addr = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx)->addr;
        uint32_t len = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx++)->len;

        if (!process_packet(xsk, addr, len))
            xsk_free_umem_frame(xsk, addr);
    }

    xsk_ring_cons__release(&xsk->rx, rcvd);

    /* Do we need to wake up the kernel for transmission */
    complete_tx(xsk);
}

static void rx_and_process(struct config *cfg, struct xsk_socket_info *xsk_socket)
{
    struct pollfd fds[2];
    int ret, nfds = 1;

    memset(fds, 0, sizeof(fds));
    fds[0].fd = xsk_socket__fd(xsk_socket->xsk);
    fds[0].events = POLLIN;

    while (!global_exit)
    {
        if (cfg->xsk_poll_mode)
        {
            ret = poll(fds, nfds, -1);
            if (ret <= 0 || ret > 1)
                continue;
        }
        handle_receive_packets(xsk_socket);
    }
}

static void exit_application(int signal)
{
    int err;

    cfg.unload_all = true;
    err = do_unload(&cfg);
    if (err)
    {
        fprintf(stderr, "Couldn't detach XDP program on iface '%s' : (%d)\n", cfg.ifname, err);
    }

    signal = signal;
    global_exit = true;
}

int main(int argc, char **argv)
{
    void *packet_buffer;
    uint64_t packet_buffer_size;
    DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts);
    DECLARE_LIBXDP_OPTS(xdp_program_opts, xdp_opts, 0);
    struct rlimit rlim = {RLIM_INFINITY, RLIM_INFINITY};
    struct xsk_umem_info *umem;
    struct xsk_socket_info *xsk_socket;
    int err;
    char errmsg[1024];

    /* Global shutdown handler */
    signal(SIGINT, exit_application);

    /* Cmdline options can change progname */
    parse_cmdline_args(argc, argv, long_options, &cfg, __doc__);

    /* Required option */
    if (cfg.ifindex == -1)
    {
        fprintf(stderr, "ERROR: Required option --dev missing\n\n");
        usage(argv[0], __doc__, long_options, (argc == 1));
        return EXIT_FAIL_OPTION;
    }

    /* Load custom program if configured */
    if (cfg.filename[0] != 0)
    {
        struct bpf_map *map;

        custom_xsk = true;
        xdp_opts.open_filename = cfg.filename;
        xdp_opts.prog_name = cfg.progname;
        xdp_opts.opts = &opts;

        if (cfg.progname[0] != 0)
        {
            xdp_opts.open_filename = cfg.filename;
            xdp_opts.prog_name = cfg.progname;
            xdp_opts.opts = &opts;

            prog = xdp_program__create(&xdp_opts);
        }
        else
        {
            prog = xdp_program__open_file(cfg.filename, NULL, &opts);
        }
        err = libxdp_get_error(prog);
        if (err)
        {
            libxdp_strerror(err, errmsg, sizeof(errmsg));
            fprintf(stderr, "ERR: loading program: %s\n", errmsg);
            return err;
        }

        err = xdp_program__attach(prog, cfg.ifindex, cfg.attach_mode, 0);
        if (err)
        {
            libxdp_strerror(err, errmsg, sizeof(errmsg));
            fprintf(stderr, "Couldn't attach XDP program on iface '%s' : %s (%d)\n", cfg.ifname, errmsg, err);
            return err;
        }

        /* We also need to load the xsks_map */
        map = bpf_object__find_map_by_name(xdp_program__bpf_obj(prog), "xsks_map");
        xsk_map_fd = bpf_map__fd(map);
        if (xsk_map_fd < 0)
        {
            fprintf(stderr, "ERROR: no xsks map found: %s\n", strerror(xsk_map_fd));
            exit(EXIT_FAILURE);
        }
    }

    /* Allow unlimited locking of memory, so all memory needed for packet
     * buffers can be locked.
     */
    if (setrlimit(RLIMIT_MEMLOCK, &rlim))
    {
        fprintf(stderr, "ERROR: setrlimit(RLIMIT_MEMLOCK) \"%s\"\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Allocate memory for NUM_FRAMES of the default XDP frame size */
    packet_buffer_size = NUM_FRAMES * FRAME_SIZE;
    if (posix_memalign(&packet_buffer, getpagesize(), packet_buffer_size))
    {
        fprintf(stderr, "ERROR: Can't allocate buffer memory \"%s\"\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Initialize shared packet_buffer for umem usage */
    umem = configure_xsk_umem(packet_buffer, packet_buffer_size);
    if (umem == NULL)
    {
        fprintf(stderr, "ERROR: Can't create umem \"%s\"\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Open and configure the AF_XDP (xsk) socket */
    xsk_socket = xsk_configure_socket(&cfg, umem);
    if (xsk_socket == NULL)
    {
        fprintf(stderr, "ERROR: Can't setup AF_XDP socket \"%s\"\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    /* Receive and count packets then drop them */
    rx_and_process(&cfg, xsk_socket);

    /* Cleanup */
    xsk_socket__delete(xsk_socket->xsk);
    xsk_umem__delete(umem->umem);

    return EXIT_OK;
}
