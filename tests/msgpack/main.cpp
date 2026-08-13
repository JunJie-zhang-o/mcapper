#include <mcapper/flight_logger.hpp>
#include <mcapper/record.hpp>
#include <mcapper/logger_options.hpp>

#include <msgpack.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <array>
#include <chrono>
#include <cmath>
#include <thread>
#include <csignal>
#include <atomic>

std::atomic<bool> g_keep_running{true};

void sigintHandler(int) {
    g_keep_running = false;
}

// 定义常量
constexpr std::size_t kArmMaxJointNum = 7;

// 定义复杂的结构体
struct ArmVar
{
    std::array<int, kArmMaxJointNum>    var1{};
    std::array<int, kArmMaxJointNum>    var2{};
    std::array<int, kArmMaxJointNum>    var3{};
    std::array<int, kArmMaxJointNum>    var4{};
    std::array<int, kArmMaxJointNum>    var5{};
    std::array<double, kArmMaxJointNum> var6{};
    std::array<double, kArmMaxJointNum> var7{};
    std::array<double, kArmMaxJointNum> var8{};
    std::array<double, kArmMaxJointNum> var9{};
    std::array<double, kArmMaxJointNum> var10{};

    // 让 msgpack-c 支持该结构体
    MSGPACK_DEFINE(var1, var2, var3, var4, var5, var6, var7, var8, var9, var10);
};

struct ArmCMD
{
    std::array<double, kArmMaxJointNum>        offset_torque{};
    std::array<double, kArmMaxJointNum>        offset_vel{};
    std::array<std::uint8_t, kArmMaxJointNum>  mode_operation{};
    std::array<std::uint16_t, kArmMaxJointNum> ctrlwd{};
    std::array<double, kArmMaxJointNum>        cmd_pos{};
    std::array<double, kArmMaxJointNum>        cmd_vel{};
    std::array<double, kArmMaxJointNum>        cmd_tau{};
    unsigned long long int                     downTime{0};

    // 让 msgpack-c 支持该结构体
    MSGPACK_DEFINE(offset_torque, offset_vel, mode_operation, ctrlwd, cmd_pos, cmd_vel, cmd_tau, downTime);
};

// 用一个顶层结构体把它们包起来（可选），或者你可以直接单独发送 ArmCMD
struct RobotData
{
    ArmVar var;
    ArmCMD cmd;

    MSGPACK_DEFINE(var, cmd);
};

template <typename T, std::size_t N>
void packNamedArray(msgpack::packer<msgpack::sbuffer>& packer,
                    const char* name,
                    const std::array<T, N>& values)
{
    packer.pack(std::string(name));
    packer.pack_array(N);
    for (const auto& value : values) {
        packer.pack(value);
    }
}

void packArmVar(msgpack::packer<msgpack::sbuffer>& packer, const ArmVar& var)
{
    packer.pack_map(10);
    packNamedArray(packer, "var1", var.var1);
    packNamedArray(packer, "var2", var.var2);
    packNamedArray(packer, "var3", var.var3);
    packNamedArray(packer, "var4", var.var4);
    packNamedArray(packer, "var5", var.var5);
    packNamedArray(packer, "var6", var.var6);
    packNamedArray(packer, "var7", var.var7);
    packNamedArray(packer, "var8", var.var8);
    packNamedArray(packer, "var9", var.var9);
    packNamedArray(packer, "var10", var.var10);
}

void packArmCMD(msgpack::packer<msgpack::sbuffer>& packer, const ArmCMD& cmd)
{
    packer.pack_map(8);
    packNamedArray(packer, "offset_torque", cmd.offset_torque);
    packNamedArray(packer, "offset_vel", cmd.offset_vel);
    packNamedArray(packer, "mode_operation", cmd.mode_operation);
    packNamedArray(packer, "ctrlwd", cmd.ctrlwd);
    packNamedArray(packer, "cmd_pos", cmd.cmd_pos);
    packNamedArray(packer, "cmd_vel", cmd.cmd_vel);
    packNamedArray(packer, "cmd_tau", cmd.cmd_tau);
    packer.pack(std::string("downTime"));
    packer.pack(cmd.downTime);
}

void packRobotData(msgpack::sbuffer& sbuf, const RobotData& data)
{
    msgpack::packer<msgpack::sbuffer> packer(sbuf);
    packer.pack_map(2);
    packer.pack(std::string("var"));
    packArmVar(packer, data.var);
    packer.pack(std::string("cmd"));
    packArmCMD(packer, data.cmd);
}

int main()
{
    std::signal(SIGINT, sigintHandler);

    mcapper::LoggerOptions opts;
    opts.output_directory = ".";
    opts.base_filename = "msgpack_complex_test";
    opts.pre_trigger_duration_ns = 3600ULL * 1000000000ULL; 
    opts.post_trigger_duration_ns = 0;
    opts.ring_buffer_capacity = 100000;

    mcapper::FlightLogger logger(opts);
    std::cout << "[INFO] Initialized mcapper::FlightLogger for complex struct test." << std::endl;
    std::cout << "[INFO] Generating data... Press Ctrl+C to exit and save MCAP file." << std::endl;

    int counter = 0;
    double t = 0.0;
    const double dt = 0.1;

    while (g_keep_running) {
        RobotData data;
        
        // 构造虚拟数据，各个关节的数据赋予不同相位的 sin 曲线
        for (std::size_t i = 0; i < kArmMaxJointNum; ++i) {
            double phase = t + (i * 0.5); 
            
            // 填充 ArmVar 的字段
            data.var.var1[i] = static_cast<int>(std::sin(phase) * 100);
            data.var.var6[i] = std::sin(phase);
            
            // 填充 ArmCMD 的字段
            data.cmd.cmd_pos[i] = std::sin(phase);
            data.cmd.cmd_vel[i] = std::cos(phase);
            data.cmd.cmd_tau[i] = std::sin(phase) * 10.0;
            data.cmd.mode_operation[i] = 8;
            data.cmd.ctrlwd[i] = 0x0F;
        }
        data.cmd.downTime = static_cast<unsigned long long>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        // a. 序列化
        msgpack::sbuffer sbuf;
        packRobotData(sbuf, data);

        // b. 组装 Record
        mcapper::Record record;
        record.log_time_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                 std::chrono::steady_clock::now().time_since_epoch()).count();
        record.publish_time_ns = record.log_time_ns;
        
        record.topic = "/robot_complex_data";
        record.message_encoding = "msgpack";
        record.schema_name = "RobotData";
        record.schema_encoding = "msgpack"; 
        record.schema_data.clear();

        const uint8_t* ptr = reinterpret_cast<const uint8_t*>(sbuf.data());
        record.data.assign(ptr, ptr + sbuf.size());

        logger.push(std::move(record));

        if (++counter % 10 == 0) {
            std::cout << "[INFO] Generated " << counter << " complex points (Size per msg: " << sbuf.size() << " bytes)..." << std::endl;
        }

        t += dt;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "\n[INFO] Exiting loop. Generated total " << counter << " points." << std::endl;
    std::cout << "[INFO] Triggering logger to flush to disk..." << std::endl;
    
    logger.trigger();
    logger.stop();
    
    std::cout << "[INFO] Saved MCAP to: " << logger.last_output_path() << std::endl;

    return 0;
}
