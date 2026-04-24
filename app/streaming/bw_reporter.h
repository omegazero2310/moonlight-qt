// app/streaming/bw_reporter.h
#pragma once
#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include <atomic>
#include <cstdint>

// BwReporter lives on the Moonlight client side.
// Measures RTP packet loss and RTT, sends a RTCP APP "BWFB" packet to the
// Sunshine host every 200ms. Thread-safe: RTP counters updated via atomics.
class BwReporter : public QObject {
    Q_OBJECT
public:
    explicit BwReporter(const QString& hostAddress, quint16 rtcpPort,
                        QObject* parent = nullptr);
    ~BwReporter() override;

    void start();
    void stop();

    // Called from the RTP receive path for each video packet received.
    void onPacketReceived(uint16_t seqNum, uint32_t send_ts, uint32_t recv_ts_us);

private slots:
    void sendReport();

private:
    QString         m_hostAddress;
    quint16         m_rtcpPort;
    QUdpSocket*     m_socket = nullptr;
    QTimer*         m_timer  = nullptr;

    std::atomic<uint32_t> m_pkts_received{0};
    std::atomic<uint32_t> m_expected_seq{0};
    std::atomic<uint32_t> m_pkts_expected{0};
    std::atomic<uint32_t> m_smoothed_rtt_us{5000};

    uint32_t m_prev_transit = 0;
    float    m_jitter_us    = 0.0f;
    uint32_t m_ssrc = 0x42574642;  // "BWFB" as a numeric SSRC
};