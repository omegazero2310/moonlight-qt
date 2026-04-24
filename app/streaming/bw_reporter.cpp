// app/streaming/bw_reporter.cpp
#include "bw_reporter.h"
#include <cstring>
#include <chrono>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#endif

static const char BWFB_NAME[4] = {'B','W','F','B'};

static void serialize_bwfb(uint8_t* buf, uint32_t ssrc,
                           uint32_t pkts_expected, uint32_t pkts_recv,
                           uint32_t rtt_us, uint32_t jitter_us,
                           uint32_t send_ts_us) {
    buf[0] = 0x80; buf[1] = 204; buf[2] = 0x00; buf[3] = 0x07;
    buf[4] = (ssrc>>24)&0xFF; buf[5] = (ssrc>>16)&0xFF;
    buf[6] = (ssrc>> 8)&0xFF; buf[7] = (ssrc    )&0xFF;
    std::memcpy(buf + 8, BWFB_NAME, 4);
    auto* p = reinterpret_cast<uint32_t*>(buf + 12);
    p[0] = htonl(pkts_expected); p[1] = htonl(pkts_recv);
    p[2] = htonl(rtt_us);        p[3] = htonl(jitter_us);
    p[4] = htonl(send_ts_us);    p[5] = 0;
}

BwReporter::BwReporter(const QString& hostAddress, quint16 rtcpPort, QObject* parent)
    : QObject(parent), m_hostAddress(hostAddress), m_rtcpPort(rtcpPort) {}

BwReporter::~BwReporter() { stop(); }

void BwReporter::start() {
    m_socket = new QUdpSocket(this);
    m_timer  = new QTimer(this);
    m_timer->setInterval(200);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &BwReporter::sendReport);
    m_timer->start();
}

void BwReporter::stop() {
    if (m_timer)  { m_timer->stop();  delete m_timer;  m_timer  = nullptr; }
    if (m_socket) {                   delete m_socket; m_socket = nullptr; }
}

void BwReporter::onPacketReceived(uint16_t seqNum, uint32_t send_ts,
                                  uint32_t recv_ts_us) {
    m_pkts_received.fetch_add(1, std::memory_order_relaxed);

    uint32_t prev = m_expected_seq.load(std::memory_order_relaxed);
    if (prev == 0) {
        m_expected_seq.store(seqNum + 1, std::memory_order_relaxed);
        m_pkts_expected.store(1, std::memory_order_relaxed);
    } else {
        uint16_t gap = seqNum - static_cast<uint16_t>(prev);
        m_pkts_expected.fetch_add(gap == 0 ? 1 : gap, std::memory_order_relaxed);
        m_expected_seq.store(seqNum + 1, std::memory_order_relaxed);
    }

    uint32_t transit = recv_ts_us - send_ts;
    if (m_prev_transit != 0) {
        int32_t d = static_cast<int32_t>(transit) - static_cast<int32_t>(m_prev_transit);
        if (d < 0) d = -d;
        m_jitter_us += (static_cast<float>(d) - m_jitter_us) / 16.0f;
    }
    m_prev_transit = transit;
}

void BwReporter::sendReport() {
    if (!m_socket) return;
    uint32_t now_us = static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count() & 0xFFFFFFFF);

    uint8_t buf[32];
    serialize_bwfb(buf, m_ssrc,
                   m_pkts_expected.load(std::memory_order_relaxed),
                   m_pkts_received.load(std::memory_order_relaxed),
                   m_smoothed_rtt_us.load(std::memory_order_relaxed),
                   static_cast<uint32_t>(m_jitter_us), now_us);

    m_socket->writeDatagram(reinterpret_cast<const char*>(buf), 32,
                            QHostAddress(m_hostAddress), m_rtcpPort);
}