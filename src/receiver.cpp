// RECEIVER — dedup + instant forward + gap-triggered NACKs. C++20, stdlib only.
//
// Ports (all 127.0.0.1):
//   bind 47002  <- media from sender, via hostile relay (162B: u16 BE seq + payload)
//   send 47020  -> harness player (164B: u32 BE seq rebuilt + payload)
//   send 47003  -> NACK feedback to sender, via relay
//
// NO jitter buffer: the player scores the FIRST arrival of each seq against
// its deadline (endpoints.py), order-independent — so any buffering is pure
// added latency. We dedup (relay + our own duplicates) and forward instantly.
// Gaps in the seq space are NACKed (0x4E + u16 count + u32 seqs), re-NACKed
// every ~30ms while the frame's deadline is still reachable, max 4 times.
// Feedback bytes are hard-capped at CAP_DOWN x raw so the total stays <2.0x.
#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

static constexpr int PAYLOAD = 160;
static constexpr int PKT = 2 + PAYLOAD;        // our wire: u16 BE seq + payload
static constexpr int HARNESS_PKT = 4 + PAYLOAD;   // player leg: u32 BE seq
static constexpr uint32_t MAXF = 1u << 16;     // u16 seq space (>21 min @ 20ms)
static constexpr double CAP_DOWN = 0.02;       // feedback cap, x raw bytes
static constexpr double MIN_RTT_S = 0.010;     // min plausible NACK->retx path
static constexpr double RENACK_S = 0.030;
static constexpr int MAX_NACKS = 4;
static constexpr int NACK_BATCH = 32;

static double now_s() {
    timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static double env_d(const char *k, double dflt) {
    const char *v = getenv(k); return v ? atof(v) : dflt;
}

static bool  seen[MAXF];
static uint8_t nacks[MAXF];
static float last_nack[MAXF];                  // seconds since T0 (fits float)

int main() {
    const double T0    = env_d("T0", now_s());
    const double DELAY = env_d("DELAY_MS", 60.0) / 1000.0;

    int in_fd  = socket(AF_INET, SOCK_DGRAM, 0);
    int out_fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in a{}; a.sin_family = AF_INET; a.sin_addr.s_addr = inet_addr("127.0.0.1");
    a.sin_port = htons(47002);
    if (bind(in_fd, (sockaddr *)&a, sizeof a) < 0) { perror("bind 47002"); return 1; }
    sockaddr_in player = a;  player.sin_port  = htons(47020);
    sockaddr_in feedbk = a;  feedbk.sin_port  = htons(47003);

    uint32_t max_seq = 0, base = 0;            // base: lowest possibly-missing seq
    bool any = false;
    uint64_t down_bytes = 0;

    auto deadline = [&](uint32_t seq) { return T0 + DELAY + seq * 0.020; };

    pollfd pfd = {in_fd, POLLIN, 0};
    char buf[2048];
    char nbuf[3 + 4 * NACK_BATCH];
    for (;;) {
        poll(&pfd, 1, 5);
        double t = now_s();

        if (pfd.revents & POLLIN) {
            ssize_t n;
            while ((n = recv(in_fd, buf, sizeof buf, MSG_DONTWAIT)) > 0) {
                if (n != PKT) continue;                     // chaff/noise ignored
                uint16_t s16; memcpy(&s16, buf, 2);
                uint32_t seq = ntohs(s16);
                if (seen[seq]) continue;                    // dedup
                seen[seq] = true;
                char out[HARNESS_PKT];                      // rebuild u32 header
                uint32_t be = htonl(seq);
                memcpy(out, &be, 4); memcpy(out + 4, buf + 2, PAYLOAD);
                sendto(out_fd, out, HARNESS_PKT, 0, (sockaddr *)&player,
                       sizeof player);
                if (!any || seq > max_seq) { max_seq = seq; any = true; }
            }
        }
        if (!any) continue;

        // scan the [base, max_seq] window for reachable holes -> NACK
        while (base < max_seq &&
               (seen[base] || nacks[base] >= MAX_NACKS ||
                t + MIN_RTT_S >= deadline(base)))
            base++;                                          // resolved/expired
        int cnt = 0;
        for (uint32_t j = base; j < max_seq && cnt < NACK_BATCH; j++) {
            if (seen[j] || nacks[j] >= MAX_NACKS) continue;
            if (t + MIN_RTT_S >= deadline(j)) continue;      // unrescuable
            if (nacks[j] > 0 && t - T0 - last_nack[j] < RENACK_S) continue;
            uint32_t be = htonl(j);
            memcpy(nbuf + 3 + 4 * cnt, &be, 4);
            nacks[j]++; last_nack[j] = (float)(t - T0);
            cnt++;
        }
        if (cnt > 0) {
            size_t len = 3 + 4 * (size_t)cnt;
            double raw = (double)PAYLOAD * (max_seq + 1);
            if ((double)(down_bytes + len) <= CAP_DOWN * raw) {
                nbuf[0] = 0x4E;
                uint16_t c = htons((uint16_t)cnt); memcpy(nbuf + 1, &c, 2);
                if (sendto(out_fd, nbuf, len, 0, (sockaddr *)&feedbk,
                           sizeof feedbk) == (ssize_t)len)
                    down_bytes += len;
            }
        }
    }
}
