#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>

namespace DGLab {

// 消息类型枚举
enum class MessageType {
    BIND,
    HEARTBEAT,
    MSG,
    ERROR,
    UNKNOWN
};

// 效果来源枚举（对应Minecraft模组）
enum class EffectSource {
    NONE,
    ENVIRONMENT,
    HEARTBEAT,
    DAMAGE
};

// 通道枚举
enum class Channel {
    A = 1,
    B = 2
};

// 消息结构体
struct Message {
    MessageType type;
    std::string rawMessage;
    std::map<std::string, std::string> fields;
    std::chrono::steady_clock::time_point timestamp;
};

// 通道状态
struct ChannelState {
    EffectSource source = EffectSource::NONE;
    std::string detail;
    std::string waveformId;
    int intensity = 0;
    std::chrono::steady_clock::time_point lastUpdate;
    std::chrono::steady_clock::time_point leaseUntil;
    
    void reset() {
        source = EffectSource::NONE;
        detail.clear();
        waveformId.clear();
        intensity = 0;
        lastUpdate = {};
        leaseUntil = {};
    }
};

// 波形数据类型
using WaveformData = std::vector<std::string>;
using WaveformChunks = std::vector<WaveformData>;

// 协议处理类
class Protocol {
public:
    Protocol();
    ~Protocol();
    
    // 解析接收到的消息
    Message parseMessage(const std::string& rawMessage);
    
    // 生成发送消息
    std::string createBindMessage(const std::string& clientId, const std::string& targetId);
    std::string createBindConfirm(const std::string& clientId, const std::string& targetId);
    std::string createHeartbeat(const std::string& clientId, const std::string& targetId);
    std::string createStrengthMessage(Channel channel, int strength);
    std::string createPulseMessage(Channel channel, const WaveformData& hexData);
    std::string createClearMessage(Channel channel);
    
    // 验证绑定消息
    bool validateBindMessage(const Message& msg, const std::string& expectedClientId);
    
    // 工具函数
    static std::string generateUUID();
    static std::string channelToString(Channel channel);
    static Channel stringToChannel(const std::string& str);
    static std::string effectSourceToString(EffectSource source);
    
    // 固定的客户端ID（参考DG_LAB协议）
    static constexpr const char* FIXED_CLIENT_ID = "1234-123456789-12345-12345-01";
    
private:
    std::mutex mutex_;
};

} // namespace DGLab