#include "websocket_server.h"
#include "dglab_protocol.h"
#include <nlohmann/json.hpp>
#include <android/log.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <random>
#include <sstream>
#include <iomanip>

#define LOG_TAG "DGLab-Server"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using json = nlohmann::json;

namespace DGLab {

WebSocketServer::WebSocketServer() {
    // 配置服务器
    server_.clear_access_channels(websocketpp::log::alevel::all);
    server_.clear_error_channels(websocketpp::log::elevel::all);
    
    // 初始化 ASIO
    server_.init_asio();
    
    // 设置事件处理器
    server_.set_open_handler([this](ConnectionHdl hdl) { onOpen(hdl); });
    server_.set_close_handler([this](ConnectionHdl hdl) { onClose(hdl); });
    server_.set_message_handler([this](ConnectionHdl hdl, MessagePtr msg) { onMessage(hdl, msg); });
    server_.set_fail_handler([this](ConnectionHdl hdl) { onFail(hdl); });
    
    // 设置重用地址
    server_.set_reuse_addr(true);
}

WebSocketServer::~WebSocketServer() {
    stop();
}

bool WebSocketServer::start(int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServerState::STOPPED) {
        LOGI("Server already running");
        return false;
    }
    
    port_ = port;
    state_ = ServerState::STARTING;
    
    try {
        // 绑定端口
        server_.listen(port_);
        
        // 获取本机IP
        localIp_ = detectLocalIp();
        LOGI("Local IP: %s:%d", localIp_.c_str(), port_);
        
        // 开始接受连接
        server_.start_accept();
        
        // 在新线程中运行服务器
        serverThread_ = std::thread([this]() {
            try {
                server_.run();
            } catch (const std::exception& e) {
                LOGE("Server thread exception: %s", e.what());
            }
        });
        
        state_ = ServerState::LISTENING;
        if (onStateChange_) onStateChange_(state_);
        
        LOGI("WebSocket server started on port %d", port_);
        return true;
        
    } catch (const std::exception& e) {
        LOGE("Failed to start server: %s", e.what());
        state_ = ServerState::ERROR;
        if (onStateChange_) onStateChange_(state_);
        return false;
    }
}

void WebSocketServer::stop() {
    if (state_ == ServerState::STOPPED) return;
    
    // 停止心跳
    heartbeatRunning_ = false;
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
    
    // 停止服务器
    server_.stop_listening();
    server_.stop();
    
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    
    state_ = ServerState::STOPPED;
    hasConnection_ = false;
    
    if (onStateChange_) onStateChange_(state_);
    LOGI("Server stopped");
}

bool WebSocketServer::sendMessage(const std::string& message) {
    if (!hasConnection_ || state_ < ServerState::CONNECTED) {
        return false;
    }
    
    try {
        server_.send(currentConnection_, message, websocketpp::frame::opcode::text);
        return true;
    } catch (const std::exception& e) {
        LOGE("Send error: %s", e.what());
        return false;
    }
}

ServerState WebSocketServer::getState() const { return state_; }
bool WebSocketServer::isListening() const { return state_ == ServerState::LISTENING; }
bool WebSocketServer::isConnected() const { return state_ >= ServerState::CONNECTED; }
bool WebSocketServer::isBound() const { return state_ >= ServerState::BOUND; }

void WebSocketServer::setOnMessage(OnClientMessageCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    onMessage_ = callback;
}

void WebSocketServer::setOnStateChange(OnServerStateChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    onStateChange_ = callback;
}

void WebSocketServer::setOnError(OnServerErrorCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    onError_ = callback;
}

std::string WebSocketServer::getTargetId() const { return generatedClientId_; }
std::string WebSocketServer::getConnectedAppId() const { return connectedAppId_; }
int WebSocketServer::getPort() const { return port_; }
std::string WebSocketServer::getLocalIp() const { return localIp_; }

void WebSocketServer::onOpen(ConnectionHdl hdl) {
    LOGI("New connection from client");
    
    // 保存连接
    currentConnection_ = hdl;
    hasConnection_ = true;
    state_ = ServerState::CONNECTED;
    
    // 生成UUID作为clientId
    // 使用随机数生成UUID格式
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
    
    uint32_t data[4] = { dis(gen), dis(gen), dis(gen), dis(gen) };
    char uuid[37];
    snprintf(uuid, sizeof(uuid), "%08x-%04x-%04x-%04x-%04x%08x",
             data[0],
             (data[1] >> 16) & 0xFFFF,
             ((data[1] & 0xFFFF) | 0x4000) & 0xFFFF,
             ((data[2] >> 16) | 0x8000) & 0xFFFF,
             data[2] & 0xFFFF,
             data[3]);
    generatedClientId_ = uuid;
    
    // 发送bind消息给App
    // {"type":"bind","clientId":"<UUID>","targetId":"","message":"targetId"}
    json bindMsg = {
        {"type", "bind"},
        {"clientId", generatedClientId_},
        {"targetId", ""},
        {"message", "targetId"}
    };
    
    try {
        server_.send(hdl, bindMsg.dump(), websocketpp::frame::opcode::text);
        LOGI("Sent bind message: clientId=%s", generatedClientId_.c_str());
    } catch (const std::exception& e) {
        LOGE("Failed to send bind: %s", e.what());
    }
    
    // 启动心跳
    heartbeatRunning_ = true;
    heartbeatThread_ = std::thread([this]() { heartbeatThread(); });
    
    if (onStateChange_) onStateChange_(state_);
}

void WebSocketServer::onClose(ConnectionHdl hdl) {
    LOGI("Connection closed");
    
    heartbeatRunning_ = false;
    if (heartbeatThread_.joinable()) {
        heartbeatThread_.join();
    }
    
    hasConnection_ = false;
    connectedAppId_.clear();
    state_ = ServerState::LISTENING;
    
    if (onStateChange_) onStateChange_(state_);
}

void WebSocketServer::onMessage(ConnectionHdl hdl, MessagePtr msg) {
    auto payload = msg->get_payload();
    LOGI("Received: %s", payload.c_str());
    
    handleAppMessage(payload);
    
    if (onMessage_) onMessage_(payload);
}

void WebSocketServer::onFail(ConnectionHdl hdl) {
    LOGE("Connection failed");
    state_ = ServerState::ERROR;
    if (onStateChange_) onStateChange_(state_);
}

void WebSocketServer::handleAppMessage(const std::string& message) {
    try {
        json j = json::parse(message);
        
        if (!j.contains("type")) return;
        
        std::string type = j["type"];
        
        if (type == "bind") {
            // App发来的绑定确认
            std::string msgContent = j.value("message", "");
            std::string appClientId = j.value("clientId", "");
            std::string receivedTargetId = j.value("targetId", "");
            
            LOGI("Bind message: appClientId=%s, targetId=%s, message=%s",
                 appClientId.c_str(), receivedTargetId.c_str(), msgContent.c_str());
            
            // 验证：
            // 1. message == "DGLAB"
            // 2. clientId == FIXED_CLIENT_ID（"1234-123456789-12345-12345-01"）
            // 3. targetId == 我们生成的UUID
            if (msgContent == "DGLAB" &&
                appClientId == Protocol::FIXED_CLIENT_ID &&
                receivedTargetId == generatedClientId_) {
                
                // 发送200确认
                json confirm = {
                    {"type", "bind"},
                    {"clientId", Protocol::FIXED_CLIENT_ID},
                    {"targetId", appClientId},
                    {"message", "200"},
                    {"statusCode", 200}
                };
                
                sendMessage(confirm.dump());
                
                connectedAppId_ = appClientId;
                state_ = ServerState::BOUND;
                
                LOGI("Bind successful! App ID: %s", appClientId.c_str());
                
                if (onStateChange_) onStateChange_(state_);
            } else {
                LOGE("Bind validation failed");
            }
            
        } else if (type == "heartbeat") {
            // App发来的心跳
            std::string receivedTargetId = j.value("targetId", "");
            
            // 回复心跳
            json heartbeat = {
                {"type", "heartbeat"},
                {"clientId", Protocol::FIXED_CLIENT_ID},
                {"targetId", receivedTargetId.empty() ? generatedClientId_ : receivedTargetId},
                {"message", "200"}
            };
            
            sendMessage(heartbeat.dump());
            LOGI("Heartbeat response sent");
            
        } else if (type == "msg") {
            // App发来的强度设置消息
            std::string msgContent = j.value("message", "");
            if (!msgContent.empty()) {
                // 解析强度: "strength-0+0+30+10"
                if (msgContent.substr(0, 9) == "strength-") {
                    std::vector<std::string> parts;
                    std::stringstream ss(msgContent.substr(9));
                    std::string item;
                    while (std::getline(ss, item, '+')) {
                        parts.push_back(item);
                    }
                    
                    if (parts.size() >= 4) {
                        appAMaxStrength_ = std::stoi(parts[2]);
                        appBMaxStrength_ = std::stoi(parts[3]);
                        LOGI("Strength limits: A=%d, B=%d", appAMaxStrength_, appBMaxStrength_);
                    }
                }
            }
        }
        
    } catch (const std::exception& e) {
        LOGE("Parse error: %s", e.what());
    }
}

void WebSocketServer::heartbeatThread() {
    while (heartbeatRunning_) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        
        if (!heartbeatRunning_ || !hasConnection_) break;
        
        if (state_ == ServerState::BOUND) {
            json heartbeat = {
                {"type", "heartbeat"},
                {"message", "200"},
                {"clientId", Protocol::FIXED_CLIENT_ID},
                {"targetId", generatedClientId_}
            };
            
            if (sendMessage(heartbeat.dump())) {
                LOGI("Heartbeat sent");
            }
        }
    }
}

std::string WebSocketServer::detectLocalIp() {
    // 🐾 用 UDP socket "假连接" 技巧获取本机局域网 IP，不依赖 ifaddrs 喵
    // 创建一个 UDP socket，假装要连 8.8.8.8:80，系统会自动选择本机出口 IP
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return "0.0.0.0";

    struct sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(80);
    inet_pton(AF_INET, "8.8.8.8", &dest.sin_addr);

    if (connect(sock, (struct sockaddr*)&dest, sizeof(dest)) < 0) {
        close(sock);
        return "0.0.0.0";
    }

    struct sockaddr_in localAddr{};
    socklen_t addrLen = sizeof(localAddr);
    if (getsockname(sock, (struct sockaddr*)&localAddr, &addrLen) < 0) {
        close(sock);
        return "0.0.0.0";
    }
    close(sock);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &localAddr.sin_addr, ip, sizeof(ip));
    LOGI("Detected local IP: %s", ip);
    return std::string(ip);
}

} // namespace DGLab