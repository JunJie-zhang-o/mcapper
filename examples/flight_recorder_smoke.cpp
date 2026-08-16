/**
 * @file flight_recorder_smoke.cpp
 *
 * @brief 持续推送 IMU 数据的示例程序。
 *
 * 程序启动后创建一个 C++20 std::jthread，在后台模拟 1 kHz 的 IMU 采样，
 * 以当前时钟纳秒戳为时间戳持续向 TripleRingBuffer 推送数据。
 *
 * 当收到 SIGINT / SIGTERM 信号时，程序会立即调用 FlightRecorder::trigger()
 * 触发 mcap 转储，等待写入完成后正常退出。生成的 .mcap 文件保存在
 * ./logs/ 目录下。
 *
 * @note TripleRingBuffer 的 freeze（pre/post）只在 push() 内部的
 *       service_freeze_requests() 中才真正发生（切换 active buffer）。
 *       因此后台采样线程会一直运行到 FlightRecorder::stop() 写完文件之后。
 */

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stop_token>
#include <thread>

#include <rfl.hpp>

#include "codec/utils.hpp"
#include "recorder.hpp"

// ─── 消息结构 ────────────────────────────────────────────────────────────────

struct ImuSample
{
    double ax;
    double ay;
    double az;
    double gx;
    double gy;
    double gz;
};

// ─── 全局信号标志 ──────────────────────────────────────────────────────────────

namespace
{
    volatile std::sig_atomic_t g_shutdown_signal = 0;

    void signal_handler(int signum)
    {
        g_shutdown_signal = signum;
    }
}  // namespace

// ─── 工具函数 ──────────────────────────────────────────────────────────────────

/// 返回当前单调时钟的纳秒时间戳
static inline uint64_t now_ns()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main()
{
    using namespace flightLogger;

    // ── 1. 创建 logs/ 输出目录 ───────────────────────────────────────────────
    const std::filesystem::path log_dir = "./logs";
    std::filesystem::create_directories(log_dir);

    // ── 2. 注册信号处理器 ────────────────────────────────────────────────────
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ── 3. 初始化 ring buffer ────────────────────────────────────────────────
    // 容量 4096，约 4 秒的 1 kHz 数据（可根据 pre_trigger_ns 调整）
    constexpr std::size_t kRingSize = 4096;
    TripleRingBuffer<TimedRecord<ImuSample>, kRingSize> imu_ring;

    // ── 4. 配置 FlightRecorder ───────────────────────────────────────────────
    constexpr uint64_t kSecond = 1'000'000'000ULL;

    FlightRecorderOptions options;
    options.pre_trigger_ns         = 3 * kSecond;   // 保存触发前 3 s
    options.post_trigger_ns        = 1 * kSecond;   // 继续采集触发后 1 s
    options.output_path            = log_dir.string();
    options.output_file_name       = "imu_smoke";
    options.mcap_metadata["robot"] = "smoke_robot";
    options.mcap_metadata["note"]  = "signal-triggered recording";

    FlightRecorder recorder{options};
    recorder.register_channel("/imu/data", imu_ring, MessageEncoding::Json, {{"sensor", "imu"}, {"source", "smoke"}});

    // ── 5. 启动后台采样线程 ──────────────────────────────────────────────────
    std::cout << "[mcapper] 开始采集 IMU 数据，按 Ctrl+C 触发转储并退出 ...\n";

    constexpr auto   kSampleInterval = std::chrono::milliseconds(1);  // ~1 kHz
    constexpr double kDeltaPhase     = 0.01;
    std::atomic<uint64_t> sample_count{0};

    std::jthread sample_thread([&](std::stop_token stop_token) {
        double phase = 0.0;

        while (!stop_token.stop_requested())
        {
            const uint64_t ts = now_ns();

            ImuSample sample{
                .ax = std::sin(phase),
                .ay = std::cos(phase),
                .az = 9.81,
                .gx = 0.1 * std::sin(2.0 * phase),
                .gy = 0.1 * std::cos(2.0 * phase),
                .gz = 0.05,
            };

            imu_ring.push(TimedRecord<ImuSample>{ts, sample});

            const uint64_t count = sample_count.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count % 1000 == 0)
                std::cout << "[mcapper] 已推送 " << count << " 条数据\r" << std::flush;

            std::this_thread::sleep_for(kSampleInterval);
            phase += kDeltaPhase;
        }
    });

    // ── 6. 主线程等待退出信号 ────────────────────────────────────────────────
    while (!g_shutdown_signal) std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // ── 7. 收到信号 → 触发转储 ───────────────────────────────────────────────
    std::cout << "\n[mcapper] 收到退出信号，触发 mcap 转储 ...\n";

    const std::string reason = g_shutdown_signal == SIGTERM ? "SIGTERM" : "SIGINT";
    recorder.trigger(reason);

    // stop() 阻塞直到 worker 完成 post_trigger 采集并把文件写完
    recorder.stop();

    sample_thread.request_stop();
    sample_thread.join();

    std::cout << "[mcapper] 转储完成，文件保存在: "
              << std::filesystem::absolute(log_dir) << "\n";
    std::cout << "[mcapper] 共采集 " << sample_count.load(std::memory_order_relaxed) << " 条 IMU 样本\n";

    return 0;
}
