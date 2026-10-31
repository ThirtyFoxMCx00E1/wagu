// SPDX-License-Identifier: GPL-2.0
// MP Multiplayer - XDP fast path for the 24/7 game servers (3.37 VPS update)
//
// Runs on the VPS network card BEFORE the Linux network stack. Only UDP to the MP game ports
// (1922 MP LAN Server, 1930 Brief Case War, 1931 MW3, 1932 Mega Vil, + any port in mp_ports) is looked at;
// everything else (SSH, MQTT 1883, WebSocket, TLS) passes untouched.
//
// What it drops (so floods never fill mp_server's socket queue = no lag spike for the real players):
//   - empty UDP packets and packets bigger than the server's 1024 byte receive buffer
//   - packet type 0 (no MP packet uses it)
//   - one source IP sending more than `pps` packets per second (token bucket per IP, LRU map).
//     The default is high (20000/s) because many phones on mobile data share one carrier IP (CGNAT):
//     one phone sends ~20-60 packets/s, so 20000/s is hundreds of phones. Floods are 100k+/s.
//   - IPs in the allow map (other MP region VPSes, the bridge) are never limited.
// Counters per action are in the `stats` map (mp-xdp.sh status).
//
// Build: clang -O2 -g -target bpf -c mp_xdp.c -o mp_xdp.o   (mp-xdp.sh does this on the VPS)
#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/in.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>

#define MP_MAX_PAYLOAD 1024
#define NSEC 1000000000ULL

struct cfg { __u32 pps; __u32 burst; };
struct bucket { __u64 last_ns; __u64 tokens; };   // tokens are scaled by NSEC (no division in the hot path)

// key 0: the limits (set by mp-xdp.sh, defaults below when it is empty)
struct { __uint(type, BPF_MAP_TYPE_ARRAY); __uint(max_entries, 1); __type(key, __u32); __type(value, struct cfg); } mp_cfg SEC(".maps");
// game ports (host byte order) -> 1
struct { __uint(type, BPF_MAP_TYPE_HASH); __uint(max_entries, 64); __type(key, __u16); __type(value, __u8); } mp_ports SEC(".maps");
// never limited (IPv4, network byte order) -> 1
struct { __uint(type, BPF_MAP_TYPE_HASH); __uint(max_entries, 256); __type(key, __u32); __type(value, __u8); } mp_allow SEC(".maps");
// per source IP token bucket; LRU: old IPs fall out by themselves (no cleanup job, fixed memory)
struct { __uint(type, BPF_MAP_TYPE_LRU_HASH); __uint(max_entries, 65536); __type(key, __u32); __type(value, struct bucket); } mp_rate SEC(".maps");
// 0 passed game packets, 1 dropped bad size, 2 dropped bad type, 3 dropped rate limit
struct { __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY); __uint(max_entries, 4); __type(key, __u32); __type(value, __u64); } stats SEC(".maps");

static __always_inline int count(__u32 k, int verdict)
{
    __u64 *c = bpf_map_lookup_elem(&stats, &k);
    if (c) *c += 1;
    return verdict;
}

SEC("xdp")
int mp_xdp(struct xdp_md *ctx)
{
    void *data = (void *)(long)ctx->data, *end = (void *)(long)ctx->data_end;
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > end || eth->h_proto != bpf_htons(ETH_P_IP)) return XDP_PASS;
    struct iphdr *ip = (void *)(eth + 1);
    if ((void *)(ip + 1) > end || ip->protocol != IPPROTO_UDP || ip->ihl < 5) return XDP_PASS;
    if (ip->frag_off & bpf_htons(0x1FFF)) return XDP_PASS;          // later fragments: no UDP header
    struct udphdr *udp = (void *)ip + ip->ihl * 4;
    if ((void *)(udp + 1) > end) return XDP_PASS;
    __u16 dport = bpf_ntohs(udp->dest);
    if (!bpf_map_lookup_elem(&mp_ports, &dport)) return XDP_PASS;   // not a game port

    __u32 src = ip->saddr;
    if (bpf_map_lookup_elem(&mp_allow, &src)) return count(0, XDP_PASS);

    __u16 ulen = bpf_ntohs(udp->len);
    if (ulen <= sizeof(*udp) || ulen - sizeof(*udp) > MP_MAX_PAYLOAD) return count(1, XDP_DROP);
    __u8 *type = (void *)(udp + 1);
    if ((void *)(type + 1) > end) return count(1, XDP_DROP);
    if (*type == 0) return count(2, XDP_DROP);

    __u32 k0 = 0; struct cfg *c = bpf_map_lookup_elem(&mp_cfg, &k0);
    __u64 pps = (c && c->pps) ? c->pps : 20000, burst = (c && c->burst) ? c->burst : pps;
    if (pps >= 0xFFFFFFFFULL) return count(0, XDP_PASS);              // limit switched off
    __u64 now = bpf_ktime_get_ns(), cap = burst * NSEC;
    struct bucket *b = bpf_map_lookup_elem(&mp_rate, &src);
    if (!b) {
        struct bucket nb = { .last_ns = now, .tokens = cap - NSEC };
        bpf_map_update_elem(&mp_rate, &src, &nb, BPF_ANY);
        return count(0, XDP_PASS);
    }
    __u64 dt = now - b->last_ns; b->last_ns = now;
    if (dt > NSEC) dt = NSEC;                                         // a quiet second refills the bucket
    __u64 t = b->tokens + dt * pps;
    if (t > cap) t = cap;
    if (t < NSEC) { b->tokens = t; return count(3, XDP_DROP); }
    b->tokens = t - NSEC;
    return count(0, XDP_PASS);
}

char _license[] SEC("license") = "GPL";
