#include "dglab_protocol.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <regex>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace DGLab {

Protocol::Protocol() = default;
Protocol::~Protocol() = default;

Message Protocol::parseMessage(const std::string& rawMessage) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    Message msg;
    msg.rawMessage = rawMessage;
    msg.timestamp = std::chrono::steady_clock::now();
    msg.type = MessageType::UNKNOWN;
    
    try {
        auto jsonMsg = json::parse(rawMessage);
        
        if (!jsonMsg.contains("type")) {
            return msg;
        }
        
        std::string type = jsonMsg["type"];
        
        if (type == "bind") {
            msg.type = MessageType::BIND;
        } else if (type == "heartbeat") {
            msg.type = MessageType::HEARTBEAT;
        } else if (type == "msg") {
            msg.type = MessageType::MSG;
        } else if (type == "error") {
            msg.type = MessageType::ERROR;
        }
        
        // 提取所有字段
        for (auto& [key, value] : jsonMsg.items()) {
            if (value.is_string()) {
                msg.fields[key] = value.get<std::string>();
            } else if (value.is_number()) {
                msg.fields[key] = std::to_string(value.get<int>());
            }
        }
        
    } catch (const std::exception& e) {
        // 解析失败，保持UNKNOWN类型
    }
    
    return msg;
}

std::string Protocol::createBindMessage(const std::string& clientId, const std::string& targetId) {
    json msg = {
        {"type", "bind"},
        {"clientId", clientId},
        {"targetId", targetId},
        {"message", "targetId"}
    };
    return msg.dump();
}

std::string Protocol::createBindConfirm(const std::string& clientId, const std::string& targetId) {
    json msg = {
        {"type", "bind"},
        {"clientId", clientId},
        {"targetId", targetId},
        {"message", "200"},
        {"statusCode", 200}
    };
    return msg.dump();
}

std::string Protocol::createHeartbeat(const std::string& clientId, const std::string& targetId) {
    json msg = {
        {"type", "heartbeat"},
        {"clientId", clientId},
        {"targetId", targetId},
        {"message", "200"}
    };
    return msg.dump();
}

std::string Protocol::createStrengthMessage(Channel channel, int strength) {
    int channelNum = static_cast<int>(channel);
    std::string message = "strength-" + std::to_string(channelNum) + "+2+" + std::to_string(strength);
    
    json msg = {
        {"type", "msg"},
        {"message", message},
        {"clientId", FIXED_CLIENT_ID},
        {"targetId", ""}
    };
    return msg.dump();
}

std::string Protocol::createPulseMessage(Channel channel, const WaveformData& hexData) {
    std::string channelStr = channelToString(channel);
    
    // 构造pulse消息
    std::string pulseData = "pulse-" + channelStr + ":[";
    for (size_t i = 0; i < hexData.size(); ++i) {
        if (i > 0) pulseData += ",";
        
        std::string hex = hexData[i];
        // 转换为大写
        std::transform(hex.begin(), hex.end(), hex.begin(), ::toupper);
        // 补齐到16位
        while (hex.length() < 16) hex = "0" + hex;
        // 截断超过16位
        if (hex.length() > 16) hex = hex.substr(0, 16);
        
        pulseData += "\"" + hex + "\"";
    }
    pulseData += "]";
    
    json msg = {
        {"type", "msg"},
        {"message", pulseData},
        {"clientId", FIXED_CLIENT_ID},
        {"targetId", ""}
    };
    return msg.dump();
}

std::string Protocol::createClearMessage(Channel channel) {
    int channelNum = static_cast<int>(channel);
    std::string message = "clear-" + std::to_string(channelNum);
    
    json msg = {
        {"type", "msg"},
        {"message", message},
        {"clientId", FIXED_CLIENT_ID},
        {"targetId", ""}
    };
    return msg.dump();
}

bool Protocol::validateBindMessage(const Message& msg, const std::string& expectedClientId) {
    if (msg.type != MessageType::BIND) {
        return false;
    }
    
    // 检查必要字段
    auto clientIdIt = msg.fields.find("clientId");
    auto targetIdIt = msg.fields.find("targetId");
    auto messageIt = msg.fields.find("message");
    
    if (clientIdIt == msg.fields.end() || 
        targetIdIt == msg.fields.end() || 
        messageIt == msg.fields.end()) {
        return false;
    }
    
    // 验证协议格式
    return messageIt->second == "DGLAB" && 
           clientIdIt->second == FIXED_CLIENT_ID &&
           targetIdIt->second == expectedClientId;
}

std::string Protocol::generateUUID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
    
    uint32_t data[4] = { dis(gen), dis(gen), dis(gen), dis(gen) };
    
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << data[0] << "-";
    ss << std::setw(4) << (data[1] >> 16) << "-";
    ss << std::setw(4) << ((data[1] & 0xFFFF) | 0x4000) << "-";
    ss << std::setw(4) << ((data[2] >> 16) | 0x8000) << "-";
    ss << std::setw(4) << (data[2] & 0xFFFF);
    ss << std::setw(8) << data[3];
    
    return ss.str();
}

std::string Protocol::channelToString(Channel channel) {
    switch (channel) {
        case Channel::A: return "A";
        case Channel::B: return "B";
        default: return "A";
    }
}

Channel Protocol::stringToChannel(const std::string& str) {
    if (str == "A" || str == "1") return Channel::A;
    if (str == "B" || str == "2") return Channel::B;
    return Channel::A;
}

std::string Protocol::effectSourceToString(EffectSource source) {
    switch (source) {
        case EffectSource::NONE: return "none";
        case EffectSource::ENVIRONMENT: return "environment";
        case EffectSource::HEARTBEAT: return "heartbeat";
        case EffectSource::DAMAGE: return "damage";
        default: return "unknown";
    }
}

} // namespace DGLab