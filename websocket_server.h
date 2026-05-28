#pragma once

#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <map>

// WebSocket++ standalone 模式
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/common/thread.hpp>
#include <websocketpp/common/memory.hpp>

namespace DGLab {

class Protocol;

// 连接状态枚举
enum class ServerState {
    STOPPED,       // 服务器未启动
    STARTING,      // 启动中
    LISTENING,     // 正在监听，等待App连接
    CONNECTED,     // App已连接（未绑定）
    BOUND,         // App已绑定（可以通信了）
    ERROR          // 出错
};

// 事件回调类型
using OnClientMessageCallback = std::function<void(const std::string&)>;
using OnServerStateChangeCallback = std::function<void(ServerState)>;
using OnServerErrorCallback = std::function<void(const std::string&)>;

// WebSocket 服务器类
// 核心：启动本地 ws 服务器，等 App 扫码连过来
class WebSocketServer {
public:
    WebSocketServer();
    ~WebSocketServer();

    // 启动/停止服务器
    bool start(int port);
    void stop();

    // 发送消息给已连接的App
    bool sendMessage(const std::string& message);

    // 获取状态
    ServerState getState() const;
    bool isListening() const;
    bool isConnected() const;  // App已连上
    bool isBound() const;      // 绑定完成，可以控制设备了

    // 设置回调
    void setOnMessage(OnClientMessageCallback callback);
    void setOnStateChange(OnServerStateChangeCallback callback);
    void setOnError(OnServerErrorCallback callback);

    // 获取连接信息
    std::string getTargetId() const;       // 我们生成的 UUID
    std::string getConnectedAppId() const; // App 的 clientId
    int getPort() const;
    std::string getLocalIp() const;        // 自动获取局域网IP

    // 获取 App 设置的最大强度
    int getAppAMaxStrength() const { return appAMaxStrength_; }
    int getAppBMaxStrength() const { return appBMaxStrength_; }
    void setAppMaxStrength(int a, int b) { appAMaxStrength_ = a; appBMaxStrength_ = b; }

private:
    // WebSocket++ 类型
    using Server = websocketpp::server<websocketpp::config::asio>;
    using MessagePtr = websocketpp::config::asio::message_type::ptr;
    using ConnectionHdl = websocketpp::connection_hdl;

    // 事件处理器
    void onOpen(ConnectionHdl hdl);
    void onClose(ConnectionHdl hdl);
    void onMessage(ConnectionHdl hdl, MessagePtr msg);
    void onFail(ConnectionHdl hdl);

    // 处理App消息
    void handleAppMessage(const std::string& message);

    // 心跳线程
    void heartbeatThread();

    // 获取局域网IP
    std::string detectLocalIp();

    // 服务器实例
    Server server_;
    int port_ = 8877;
    std::string localIp_;

    // 连接状态
    std::atomic<ServerState> state_{ServerState::STOPPED};
    ConnectionHdl currentConnection_;
    bool hasConnection_ = false;

    // 绑定信息
    std::string generatedClientId_;  // 我们生成的UUID（发给App）
    std::string connectedAppId_;     // App的clientId

    // App设置的强度上限
    int appAMaxStrength_ = 100;
    int appBMaxStrength_ = 100;

    // 回调
    OnClientMessageCallback onMessage_;
    OnServerStateChangeCallback onStateChange_;
    OnServerErrorCallback onError_;

    // 心跳
    std::atomic<bool> heartbeatRunning_{false};
    std::thread heartbeatThread_;
    std::thread serverThread_;

    // 线程安全
    mutable std::mutex mutex_;
};

} // namespace DGLab