#pragma once
#include <string>
#include <vector>
#include <functional>
#include <imgui.h>

namespace DGLab {

class DGLabController;

// ImGui界面配置
struct UIConfig {
    bool showConnectionPanel = true;
    bool showControlPanel = true;
    bool showWaveformPanel = true;
    bool showStatsPanel = true;
    bool showAdvancedPanel = false;
    ImVec2 windowPos = ImVec2(100, 100);
    ImVec2 windowSize = ImVec2(400, 600);
};

// 界面状态
struct UIState {
    bool isConnected = false;
    bool isBound = false;
    int strengthA = 50;
    int strengthB = 50;
    std::string selectedWaveform = "heartbeat";
    std::string serverAddress = "ws://192.168.1.100:8080";
    bool syncChannels = true;
    bool showAdvanced = false;
    std::string statusText = "未连接";
    ImVec4 statusColor = ImVec4(1.0f, 0.0f, 0.0f, 1.0f); // 红色
};

// ImGui集成类
class ImGuiDGLabUI {
public:
    ImGuiDGLabUI();
    ~ImGuiDGLabUI();
    
    // 初始化UI
    bool initialize(std::shared_ptr<DGLabController> controller);
    
    // 渲染UI（每帧调用）
    void render();
    
    // 更新状态（从控制器同步）
    void updateState();
    
    // 配置UI
    void setConfig(const UIConfig& config);
    UIConfig getConfig() const;
    
    // 获取UI状态
    UIState getUIState() const;
    
    // 回调设置
    void setOnConnect(std::function<void(const std::string&)> callback);
    void setOnDisconnect(std::function<void()> callback);
    void setOnStrengthChange(std::function<void(int, int)> callback);
    void setOnWaveformChange(std::function<void(const std::string&)> callback);
    void setOnEffectRequest(std::function<void(const std::string&, int, int)> callback);
    
private:
    // UI组件渲染
    void renderConnectionPanel();
    void renderControlPanel();
    void renderWaveformPanel();
    void renderStatsPanel();
    void renderAdvancedPanel();
    void renderStatusBar();
    
    // 工具函数
    void updateStatusText();
    void updateStatusColor();
    void applyStrength();
    void applyWaveform();
    
    // 控制器
    std::shared_ptr<DGLabController> controller_;
    
    // UI状态
    UIState uiState_;
    UIConfig uiConfig_;
    
    // 波形列表
    std::vector<std::string> waveformList_;
    int selectedWaveformIndex_ = 0;
    
    // 输入缓冲区
    char serverAddressBuf_[256] = "";
    char waveformSearchBuf_[128] = "";
    
    // 回调函数
    std::function<void(const std::string&)> onConnect_;
    std::function<void()> onDisconnect_;
    std::function<void(int, int)> onStrengthChange_;
    std::function<void(const std::string&)> onWaveformChange_;
    std::function<void(const std::string&, int, int)> onEffectRequest_;
    
    // 样式
    ImVec4 buttonColor_ = ImVec4(0.2f, 0.5f, 0.8f, 1.0f);
    ImVec4 buttonHoverColor_ = ImVec4(0.3f, 0.6f, 0.9f, 1.0f);
    ImVec4 buttonActiveColor_ = ImVec4(0.1f, 0.4f, 0.7f, 1.0f);
    ImVec4 successColor_ = ImVec4(0.0f, 0.8f, 0.0f, 1.0f);
    ImVec4 warningColor_ = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
    ImVec4 errorColor_ = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);
};

} // namespace DGLab