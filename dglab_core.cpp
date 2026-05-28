// ============================================================
//  DGLab Core 实现
//  拖进项目就能用，不需要额外配置喵
// ============================================================

#include "dglab_core.h"
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <iomanip>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "DGLab-Core"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#else
#include <cstdio>
#define LOGI(...) do { printf("[DGLab] "); printf(__VA_ARGS__); printf("\n"); } while(0)
#define LOGE(...) do { printf("[DGLab ERR] "); printf(__VA_ARGS__); printf("\n"); } while(0)
#endif

using json = nlohmann::json;

namespace DGLab {

Core::Core() {
    server_.clear_access_channels(websocketpp::log::alevel::all);
    server_.clear_error_channels(websocketpp::log::elevel::all);
    server_.init_asio();
    server_.set_reuse_addr(true);

    server_.set_open_handler([this](ConnectionHdl hdl) { onOpen(hdl); });
    server_.set_close_handler([this](ConnectionHdl hdl) { onClose(hdl); });
    server_.set_message_handler([this](ConnectionHdl hdl, MessagePtr msg) { onMessage(hdl, msg); });
    server_.set_fail_handler([this](ConnectionHdl hdl) { onFail(hdl); });
}

Core::~Core() { stop(); }

// ============================================================
//  启动/停止
// ============================================================

bool Core::start(int port) {
    if (running_) return true;
    port_ = port;
    localIp_ = detectLocalIp();

    try {
        server_.listen(port_);
        server_.start_accept();

        serverThread_ = std::thread([this]() {
            try { server_.run(); }
            catch (const std::exception& e) { LOGE("Server error: %s", e.what()); }
        });

        running_ = true;
        state_ = State::LISTENING;
        if (onStateChange_) onStateChange_(state_);

        LOGI("DGLab server started: %s:%d", localIp_.c_str(), port_);
        LOGI("QR URL: %s", getQrUrl().c_str());
        return true;
    } catch (const std::exception& e) {
        LOGE("Start failed: %s", e.what());
        state_ = State::ERROR;
        return false;
    }
}

void Core::stop() {
    if (!running_) return;
    running_ = false;

    heartbeatRunning_ = false;
    if (heartbeatThread_.joinable()) heartbeatThread_.join();

    server_.stop_listening();
    server_.stop();
    if (serverThread_.joinable()) serverThread_.join();

    hasConn_ = false;
    state_ = State::STOPPED;
    if (onStateChange_) onStateChange_(state_);
    LOGI("DGLab server stopped");
}

// ============================================================
//  状态查询
// ============================================================

State Core::getState() const { return state_; }
bool Core::isBound() const { return state_ == State::BOUND; }
bool Core::isConnected() const { return state_ >= State::CONNECTED; }
std::string Core::getLocalIp() const { return localIp_; }
int Core::getPort() const { return port_; }

std::string Core::getQrUrl() const {
    return "https://www.dungeon-lab.com/app-download.php#DGLAB-SOCKET#ws://"
           + localIp_ + ":" + std::to_string(port_) + "/" + FIXED_CLIENT_ID;
}

// ============================================================
//  发送控制指令
// ============================================================

bool Core::sendStrength(int channel, int strength) {
    return sendMsg("strength-" + std::to_string(channel) + "+2+" + std::to_string(strength));
}

bool Core::sendStrengthAB(int strengthA, int strengthB) {
    bool ok = true;
    ok &= sendStrength(1, strengthA);
    ok &= sendStrength(2, strengthB);
    return ok;
}

bool Core::sendWaveform(int channel, const std::vector<std::string>& hexData) {
    if (hexData.empty()) return false;
    std::string ch = (channel == 1) ? "A" : "B";

    // 先清空
    clearChannel(channel);

    // 发送 pulse 数据（分块，每块最多 100 帧）
    const int chunkSize = 100;
    for (size_t i = 0; i < hexData.size(); i += chunkSize) {
        size_t end = std::min(i + chunkSize, hexData.size());
        std::string pulseMsg = "pulse-" + ch + ":[";
        for (size_t j = i; j < end; j++) {
            if (j > i) pulseMsg += ",";
            std::string hex = hexData[j];
            // 转大写，补齐 16 位
            for (auto& c : hex) c = toupper(c);
            while (hex.size() < 16) hex = "0" + hex;
            if (hex.size() > 16) hex = hex.substr(0, 16);
            pulseMsg += "\"" + hex + "\"";
        }
        pulseMsg += "]";
        sendMsg(pulseMsg);
    }
    return true;
}

bool Core::sendWaveformWithStrength(int channel, const std::vector<std::string>& hexData, int strength) {
    sendWaveform(channel, hexData);
    return sendStrength(channel, strength);
}

bool Core::clearChannel(int channel) {
    return sendMsg("clear-" + std::to_string(channel));
}

bool Core::silenceAll() {
    sendStrength(1, 0);
    sendStrength(2, 0);
    clearChannel(1);
    clearChannel(2);
    return true;
}

// ============================================================
//  便捷方法
// ============================================================

void Core::triggerHit(int lostHp, int maxStrength) {
    if (!isBound()) return;
    int strength = std::min(maxStrength, lostHp * 3);
    if (strength < 1) strength = 1;
    sendStrengthAB(strength, strength);
    LOGI("triggerHit: lost=%d strength=%d", lostHp, strength);
}

void Core::triggerDeath(int strength) {
    if (!isBound()) return;
    silenceAll();
    sendStrengthAB(strength, strength);
    LOGI("triggerDeath: strength=%d", strength);
}

bool Core::sendRaw(const std::string& jsonMessage) {
    std::lock_guard<std::mutex> lock(sendMutex_);
    if (!hasConn_ || state_ < State::CONNECTED) return false;
    try {
        server_.send(currentConn_, jsonMessage, websocketpp::frame::opcode::text);
        return true;
    } catch (...) { return false; }
}

// ============================================================
//  WebSocket 事件
// ============================================================

void Core::onOpen(ConnectionHdl hdl) {
    currentConn_ = hdl;
    hasConn_ = true;
    state_ = State::CONNECTED;

    // 生成 UUID
    generatedUuid_ = generateUUID();

    // 主动发 bind 消息（和 MC Mod 一致）
    json bindMsg = {
        {"type", "bind"},
        {"clientId", generatedUuid_},
        {"targetId", ""},
        {"message", "targetId"}
    };
    try {
        server_.send(hdl, bindMsg.dump(), websocketpp::frame::opcode::text);
        LOGI("Sent bind: uuid=%s", generatedUuid_.c_str());
    } catch (...) {}

    // 启动心跳
    heartbeatRunning_ = true;
    heartbeatThread_ = std::thread([this]() { heartbeatLoop(); });

    if (onStateChange_) onStateChange_(state_);
}

void Core::onClose(ConnectionHdl hdl) {
    heartbeatRunning_ = false;
    if (heartbeatThread_.joinable()) heartbeatThread_.join();

    hasConn_ = false;
    appClientId_.clear();
    state_ = State::LISTENING;
    if (onStateChange_) onStateChange_(state_);
    LOGI("App disconnected");
}

void Core::onMessage(ConnectionHdl hdl, MessagePtr msg) {
    auto payload = msg->get_payload();
    LOGI("Recv: %s", payload.c_str());

    try {
        json j = json::parse(payload);
        std::string type = j.value("type", "");

        if (type == "bind")      handleBind(payload);
        else if (type == "heartbeat") handleHeartbeat(payload);
        else if (type == "msg")  handleMsg(payload);
    } catch (...) {}

    if (onMessage_) onMessage_(payload);
}

void Core::onFail(ConnectionHdl hdl) {
    state_ = State::ERROR;
    if (onStateChange_) onStateChange_(state_);
    LOGE("Connection failed");
}

// ============================================================
//  消息处理
// ============================================================

void Core::handleBind(const std::string& message) {
    json j = json::parse(message);
    std::string msgContent = j.value("message", "");
    std::string clientId = j.value("clientId", "");
    std::string targetId = j.value("targetId", "");

    if (msgContent == "DGLAB" && clientId == FIXED_CLIENT_ID && targetId == generatedUuid_) {
        // 验证通过，回复 200
        json confirm = {
            {"type", "bind"},
            {"clientId", FIXED_CLIENT_ID},
            {"targetId", clientId},
            {"message", "200"},
            {"statusCode", 200}
        };
        {
            std::lock_guard<std::mutex> lock(sendMutex_);
            server_.send(currentConn_, confirm.dump(), websocketpp::frame::opcode::text);
        }
        appClientId_ = clientId;
        state_ = State::BOUND;
        LOGI("Bound! App ID: %s", clientId.c_str());
        if (onStateChange_) onStateChange_(state_);
    } else {
        LOGE("Bind validation failed");
    }
}

void Core::handleHeartbeat(const std::string& message) {
    json j = json::parse(message);
    std::string targetId = j.value("targetId", "");
    sendJson("heartbeat", "200", FIXED_CLIENT_ID,
             targetId.empty() ? generatedUuid_ : targetId);
}

void Core::handleMsg(const std::string& message) {
    json j = json::parse(message);
    std::string msg = j.value("message", "");

    // 解析 App 下发的强度上限: "strength-0+0+<A>+<B>"
    if (msg.substr(0, 9) == "strength-") {
        std::vector<std::string> parts;
        std::stringstream ss(msg.substr(9));
        std::string item;
        while (std::getline(ss, item, '+')) parts.push_back(item);

        if (parts.size() >= 4) {
            appMaxA_ = std::stoi(parts[2]);
            appMaxB_ = std::stoi(parts[3]);
            LOGI("App max strength: A=%d B=%d", appMaxA_, appMaxB_);
        }
    }
}

// ============================================================
//  心跳
// ============================================================

void Core::heartbeatLoop() {
    while (heartbeatRunning_) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
        if (!heartbeatRunning_ || !hasConn_) break;
        if (state_ == State::BOUND) {
            sendJson("heartbeat", "200", FIXED_CLIENT_ID, generatedUuid_);
            LOGI("Heartbeat sent");
        }
    }
}

// ============================================================
//  内部辅助
// ============================================================

bool Core::sendJson(const std::string& type, const std::string& message,
                    const std::string& clientId, const std::string& targetId) {
    json j = {{"type", type}, {"message", message}, {"clientId", clientId}, {"targetId", targetId}};
    std::lock_guard<std::mutex> lock(sendMutex_);
    if (!hasConn_) return false;
    try {
        server_.send(currentConn_, j.dump(), websocketpp::frame::opcode::text);
        return true;
    } catch (...) { return false; }
}

bool Core::sendMsg(const std::string& message) {
    return sendJson("msg", message, FIXED_CLIENT_ID,
                    hasConn_ ? generatedUuid_ : "");
}

std::string Core::detectLocalIp() {
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

    struct sockaddr_in local{};
    socklen_t len = sizeof(local);
    if (getsockname(sock, (struct sockaddr*)&local, &len) < 0) {
        close(sock);
        return "0.0.0.0";
    }
    close(sock);

    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local.sin_addr, ip, sizeof(ip));
    return ip;
}

std::string Core::generateUUID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);

    uint32_t d[4] = {dis(gen), dis(gen), dis(gen), dis(gen)};
    char buf[37];
    snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%04x%08x",
             d[0], (d[1] >> 16) & 0xFFFF, ((d[1] & 0xFFFF) | 0x4000) & 0xFFFF,
             ((d[2] >> 16) | 0x8000) & 0xFFFF, d[2] & 0xFFFF, d[3]);
    return buf;
}

} // namespace DGLab