#pragma once

// ============================================================
//  DGLab Core —— 通用 DGLab 对接库
//
//  一个 .h + 一个 .cpp，直接拖进任何 C++17 项目就能用
//
//  功能：
//    1. WebSocket 服务器（App 扫码连过来）
//    2. DGLab 协议（绑定 / 心跳 / 强度 / 波形）
//    3. 局域网 IP 自动获取
//    4. 二维码 URL 生成（ImGui 或其他 UI 渲染）
//
//  依赖：
//    - websocketpp 0.8.2 + standalone asio 1.12.2
//    - nlohmann/json
//    - 编译时需 -DASIO_STANDALONE -D_WEBSOCKETPP_CPP11_STL_
//      -fexceptions -frtti
//
//  用法：
//    #include "dglab_core.h"
//
//    DGLab::Core dglab;
//    dglab.start(8877);
//    // ... 在 UI 里显示 dglab.getQrUrl() ...
//    // ... 用 dglab.isBound() 检查连接 ...
//    // ... 用 dglab.sendStrength() / dglab.sendWaveform() 控制 ...
// ============================================================

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <map>

// WebSocket++ standalone
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

namespace DGLab {

// ---- 连接状态 ----
enum class State {
    STOPPED,
    LISTENING,    // 等待 App 连接
    CONNECTED,    // App 已连接（未绑定）
    BOUND,        // 绑定完成，可以控制
    ERROR
};

// ---- 回调类型 ----
using OnStateChange = std::function<void(State)>;
using OnMessage     = std::function<void(const std::string&)>;

// ============================================================
//  Core 主类
// ============================================================
class Core {
public:
    Core();
    ~Core();

    // ---- 启动/停止 ----
    bool start(int port = 8877);
    void stop();

    // ---- 状态查询 ----
    State getState() const;
    bool isBound() const;         // 绑定完成 = 可以发指令
    bool isConnected() const;     // App 已连上

    // ---- 连接信息（生成二维码用） ----
    std::string getLocalIp() const;   // 自动获取的局域网 IP
    int getPort() const;
    std::string getQrUrl() const;     // App 扫码用的完整 URL

    // ---- 发送控制指令 ----

    // 设置强度 (通道 A=1, B=2, 强度 0~100)
    bool sendStrength(int channel, int strength);

    // 设置双通道强度
    bool sendStrengthAB(int strengthA, int strengthB);

    // 发送波形（hex 数组，从 waveforms/*.json 的 data 字段读取）
    bool sendWaveform(int channel, const std::vector<std::string>& hexData);

    // 发送波形 + 强度（一步到位）
    bool sendWaveformWithStrength(int channel, const std::vector<std::string>& hexData, int strength);

    // 清空通道波形队列
    bool clearChannel(int channel);

    // 清空所有 + 强度归零
    bool silenceAll();

    // ---- 便捷方法 ----

    // 受伤反馈：根据失血量自动算强度，发送到双通道
    void triggerHit(int lostHp, int maxStrength = 30);

    // 死亡反馈：高强度 + 清空重置
    void triggerDeath(int strength = 80);

    // 自定义效果：直接发 JSON 消息
    bool sendRaw(const std::string& jsonMessage);

    // ---- App 设置的强度上限（由 App 通过协议下发） ----
    int getAppMaxStrengthA() const { return appMaxA_; }
    int getAppMaxStrengthB() const { return appMaxB_; }

    // ---- 回调 ----
    void setOnStateChange(OnStateChange cb) { onStateChange_ = cb; }
    void setOnMessage(OnMessage cb) { onMessage_ = cb; }

    // ---- 固定客户端 ID（DGLab 协议规定） ----
    static constexpr const char* FIXED_CLIENT_ID = "1234-123456789-12345-12345-01";

private:
    // WebSocket++ 类型
    using Server = websocketpp::server<websocketpp::config::asio>;
    using ConnectionHdl = websocketpp::connection_hdl;
    using MessagePtr = websocketpp::config::asio::message_type::ptr;

    // 事件处理
    void onOpen(ConnectionHdl hdl);
    void onClose(ConnectionHdl hdl);
    void onMessage(ConnectionHdl hdl, MessagePtr msg);
    void onFail(ConnectionHdl hdl);

    // 消息处理
    void handleBind(const std::string& message);
    void handleHeartbeat(const std::string& message);
    void handleMsg(const std::string& message);

    // 心跳
    void heartbeatLoop();

    // 辅助
    bool sendJson(const std::string& type, const std::string& message,
                  const std::string& clientId, const std::string& targetId);
    bool sendMsg(const std::string& message);  // type=msg 的快捷方式
    std::string detectLocalIp();
    std::string generateUUID();

    // 服务器
    Server server_;
    int port_ = 8877;
    std::string localIp_;

    // 连接
    std::atomic<State> state_{State::STOPPED};
    ConnectionHdl currentConn_;
    bool hasConn_ = false;

    // 绑定信息
    std::string generatedUuid_;   // 我们生成的 UUID
    std::string appClientId_;     // App 的 clientId

    // App 强度上限
    int appMaxA_ = 100;
    int appMaxB_ = 100;

    // 线程
    std::thread serverThread_;
    std::thread heartbeatThread_;
    std::atomic<bool> heartbeatRunning_{false};
    std::atomic<bool> running_{false};

    // 回调
    OnStateChange onStateChange_;
    OnMessage onMessage_;

    // 锁
    std::mutex sendMutex_;
};

} // namespace DGLab