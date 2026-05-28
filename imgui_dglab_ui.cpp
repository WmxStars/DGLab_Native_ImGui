#include "imgui_dglab_ui.h"
#include "dglab_controller.h"
#include <android/log.h>
#include <algorithm>

#define LOG_TAG "DGLab-UI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace DGLab {

ImGuiDGLabUI::ImGuiDGLabUI() = default;
ImGuiDGLabUI::~ImGuiDGLabUI() = default;

bool ImGuiDGLabUI::initialize(std::shared_ptr<DGLabController> controller) {
    controller_ = controller;
    
    if (!controller_) {
        LOGE("Controller is null");
        return false;
    }
    
    // 同步初始状态
    updateState();
    
    // 获取可用波形列表
    waveformList_ = controller_->getAvailableWaveforms();
    
    // 设置控制器回调
    controller_->setOnStateChange([this](ControllerState state) {
        updateState();
    });
    
    controller_->setOnError([this](const std::string& err) {
        LOGI("Controller error: %s", err.c_str());
    });
    
    LOGI("ImGuiDGLabUI initialized with %zu waveforms", waveformList_.size());
    return true;
}

void ImGuiDGLabUI::render() {
    if (!controller_) return;
    
    // 每帧同步状态
    updateState();
    
    ImGui::SetNextWindowSize(ImVec2(420, 580), ImGuiCond_FirstUseEver);
    ImGui::Begin("🐾 DGLab 控制器", nullptr, ImGuiWindowFlags_NoCollapse);
    
    renderStatusBar();
    ImGui::Separator();
    
    if (uiConfig_.showConnectionPanel) renderConnectionPanel();
    if (uiConfig_.showControlPanel)    renderControlPanel();
    if (uiConfig_.showWaveformPanel)   renderWaveformPanel();
    if (uiConfig_.showStatsPanel)      renderStatsPanel();
    if (uiConfig_.showAdvancedPanel)   renderAdvancedPanel();
    
    ImGui::End();
}

void ImGuiDGLabUI::updateState() {
    if (!controller_) return;
    
    auto state = controller_->getState();
    uiState_.isConnected = (state >= ControllerState::CONNECTED);
    uiState_.isBound = (state >= ControllerState::BOUND);
    
    updateStatusText();
    updateStatusColor();
    
    // 刷新波形列表
    waveformList_ = controller_->getAvailableWaveforms();
}

void ImGuiDGLabUI::setConfig(const UIConfig& config) { uiConfig_ = config; }
UIConfig ImGuiDGLabUI::getConfig() const { return uiConfig_; }
UIState ImGuiDGLabUI::getUIState() const { return uiState_; }

void ImGuiDGLabUI::setOnConnect(std::function<void(const std::string&)> cb) { onConnect_ = cb; }
void ImGuiDGLabUI::setOnDisconnect(std::function<void()> cb) { onDisconnect_ = cb; }
void ImGuiDGLabUI::setOnStrengthChange(std::function<void(int, int)> cb) { onStrengthChange_ = cb; }
void ImGuiDGLabUI::setOnWaveformChange(std::function<void(const std::string&)> cb) { onWaveformChange_ = cb; }
void ImGuiDGLabUI::setOnEffectRequest(std::function<void(const std::string&, int, int)> cb) { onEffectRequest_ = cb; }

void ImGuiDGLabUI::renderStatusBar() {
    ImGui::TextColored(uiState_.statusColor, "● %s", uiState_.statusText.c_str());
    
    if (uiState_.isBound) {
        ImGui::SameLine(ImGui::GetWindowWidth() - 120);
        ImGui::TextColored(successColor_, "A:%d B:%d", uiState_.strengthA, uiState_.strengthB);
    }
}

void ImGuiDGLabUI::renderConnectionPanel() {
    if (!ImGui::CollapsingHeader("连接设置", ImGuiTreeNodeFlags_DefaultOpen)) return;
    
    ImGui::PushID("ConnectionPanel");
    
    // 地址输入
    strncpy(serverAddressBuf_, uiState_.serverAddress.c_str(), sizeof(serverAddressBuf_) - 1);
    serverAddressBuf_[sizeof(serverAddressBuf_) - 1] = '\0';
    
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputText("##地址", serverAddressBuf_, sizeof(serverAddressBuf_))) {
        uiState_.serverAddress = serverAddressBuf_;
    }
    
    ImGui::Spacing();
    
    // 连接/断开按钮
    if (!uiState_.isConnected) {
        ImGui::PushStyleColor(ImGuiCol_Button, buttonColor_);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonHoverColor_);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonActiveColor_);
        
        if (ImGui::Button("🔗 连接", ImVec2(-1, 36))) {
            controller_->updateConfig({uiState_.serverAddress});
            controller_->connect();
            if (onConnect_) onConnect_(uiState_.serverAddress);
        }
        
        ImGui::PopStyleColor(3);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, errorColor_);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.8f, 0.0f, 0.0f, 1.0f));
        
        if (ImGui::Button("❌ 断开", ImVec2(-1, 36))) {
            controller_->disconnect();
            if (onDisconnect_) onDisconnect_();
        }
        
        ImGui::PopStyleColor(3);
    }
    
    ImGui::PopID();
}

void ImGuiDGLabUI::renderControlPanel() {
    if (!ImGui::CollapsingHeader("强度控制", ImGuiTreeNodeFlags_DefaultOpen)) return;
    
    ImGui::PushID("ControlPanel");
    
    // 通道同步开关
    ImGui::Checkbox("通道同步", &uiState_.syncChannels);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("开启后 A/B 通道使用相同波形");
    }
    
    ImGui::Spacing();
    
    // A通道
    ImGui::Text("A 通道");
    ImGui::SameLine(80);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##强度A", &uiState_.strengthA, 0, 100)) {
        if (uiState_.isBound) {
            controller_->setStrengthA(uiState_.strengthA);
        }
    }
    
    // B通道
    ImGui::Text("B 通道");
    ImGui::SameLine(80);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##强度B", &uiState_.strengthB, 0, 100)) {
        if (uiState_.isBound) {
            controller_->setStrengthB(uiState_.strengthB);
        }
    }
    
    ImGui::Spacing();
    
    // 快捷按钮
    float btnWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
    
    if (ImGui::Button("应用", ImVec2(btnWidth, 30))) {
        if (uiState_.isBound) {
            controller_->setStrength(uiState_.strengthA, uiState_.strengthB);
            if (onStrengthChange_) onStrengthChange_(uiState_.strengthA, uiState_.strengthB);
        }
    }
    ImGui::SameLine();
    
    if (ImGui::Button("归零", ImVec2(btnWidth, 30))) {
        uiState_.strengthA = 0;
        uiState_.strengthB = 0;
        if (uiState_.isBound) {
            controller_->stopAllEffects();
        }
    }
    ImGui::SameLine();
    
    ImGui::PushStyleColor(ImGuiCol_Button, errorColor_);
    if (ImGui::Button("停止", ImVec2(btnWidth, 30))) {
        if (uiState_.isBound) {
            controller_->stopAllEffects();
        }
    }
    ImGui::PopStyleColor();
    
    ImGui::PopID();
}

void ImGuiDGLabUI::renderWaveformPanel() {
    if (!ImGui::CollapsingHeader("波形控制", ImGuiTreeNodeFlags_DefaultOpen)) return;
    
    ImGui::PushID("WaveformPanel");
    
    // 波形选择列表
    ImGui::Text("选择波形:");
    ImGui::SetNextItemWidth(-1);
    
    if (ImGui::BeginCombo("##波形", uiState_.selectedWaveform.c_str())) {
        for (size_t i = 0; i < waveformList_.size(); i++) {
            bool isSelected = (uiState_.selectedWaveform == waveformList_[i]);
            if (ImGui::Selectable(waveformList_[i].c_str(), isSelected)) {
                uiState_.selectedWaveform = waveformList_[i];
                selectedWaveformIndex_ = (int)i;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    
    ImGui::Spacing();
    
    // 发送波形按钮
    float btnWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
    
    ImGui::PushStyleColor(ImGuiCol_Button, buttonColor_);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonHoverColor_);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonActiveColor_);
    
    if (ImGui::Button("发送波形", ImVec2(btnWidth, 32))) {
        if (uiState_.isBound) {
            controller_->sendWaveform(
                uiState_.selectedWaveform,
                uiState_.strengthA,
                uiState_.strengthB
            );
            if (onEffectRequest_) {
                onEffectRequest_(uiState_.selectedWaveform, uiState_.strengthA, uiState_.strengthB);
            }
        }
    }
    ImGui::PopStyleColor(3);
    
    ImGui::SameLine();
    
    if (ImGui::Button("清空波形", ImVec2(btnWidth, 32))) {
        if (uiState_.isBound) {
            controller_->clearChannel(1);
            controller_->clearChannel(2);
        }
    }
    
    ImGui::PopID();
}

void ImGuiDGLabUI::renderStatsPanel() {
    if (!ImGui::CollapsingHeader("统计信息")) return;
    
    ImGui::PushID("StatsPanel");
    
    auto stats = controller_->getStats();
    
    ImGui::Text("发送消息: %d", stats.messagesSent);
    ImGui::Text("接收消息: %d", stats.messagesReceived);
    ImGui::Text("连接尝试: %d", stats.connectionAttempts);
    
    if (uiState_.isConnected) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - stats.connectionStart).count();
        ImGui::Text("已连接: %lld 秒", (long long)elapsed);
    }
    
    if (uiState_.isBound) {
        ImGui::Separator();
        ImGui::Text("TargetID: %s", controller_->getTargetId().c_str());
    }
    
    ImGui::PopID();
}

void ImGuiDGLabUI::renderAdvancedPanel() {
    // 高级选项（默认隐藏）
    ImGui::Checkbox("显示高级选项", &uiConfig_.showAdvancedPanel);
    if (!uiConfig_.showAdvancedPanel) return;
    
    if (!ImGui::CollapsingHeader("高级设置")) return;
    
    ImGui::PushID("AdvancedPanel");
    
    ImGui::TextDisabled("以下选项通常不需要修改");
    ImGui::Spacing();
    
    auto config = controller_->getConfig();
    
    static int heartbeatInterval = config.heartbeatIntervalMs / 1000;
    ImGui::SliderInt("心跳间隔(秒)", &heartbeatInterval, 10, 120);
    config.heartbeatIntervalMs = heartbeatInterval * 1000;
    
    static bool autoReconnect = config.autoReconnect;
    ImGui::Checkbox("自动重连", &autoReconnect);
    config.autoReconnect = autoReconnect;
    
    controller_->updateConfig(config);
    
    ImGui::Spacing();
    
    if (ImGui::Button("重置控制器")) {
        controller_->disconnect();
        controller_->initialize();
    }
    
    ImGui::PopID();
}

void ImGuiDGLabUI::updateStatusText() {
    auto state = controller_->getState();
    switch (state) {
        case ControllerState::IDLE:       uiState_.statusText = "未连接";       break;
        case ControllerState::CONNECTING:  uiState_.statusText = "连接中...";    break;
        case ControllerState::CONNECTED:   uiState_.statusText = "已连接";       break;
        case ControllerState::BOUND:       uiState_.statusText = "已绑定 ✓";    break;
        case ControllerState::ACTIVE:      uiState_.statusText = "运行中 ⚡";    break;
        case ControllerState::ERROR:       uiState_.statusText = "连接错误 ✗";  break;
    }
}

void ImGuiDGLabUI::updateStatusColor() {
    auto state = controller_->getState();
    switch (state) {
        case ControllerState::IDLE:       uiState_.statusColor = errorColor_;    break;
        case ControllerState::CONNECTING:  uiState_.statusColor = warningColor_;  break;
        case ControllerState::CONNECTED:   uiState_.statusColor = warningColor_;  break;
        case ControllerState::BOUND:       uiState_.statusColor = successColor_;  break;
        case ControllerState::ACTIVE:      uiState_.statusColor = successColor_;  break;
        case ControllerState::ERROR:       uiState_.statusColor = errorColor_;    break;
    }
}

void ImGuiDGLabUI::applyStrength() {
    if (uiState_.isBound) {
        controller_->setStrength(uiState_.strengthA, uiState_.strengthB);
    }
}

void ImGuiDGLabUI::applyWaveform() {
    if (uiState_.isBound) {
        controller_->sendWaveform(
            uiState_.selectedWaveform,
            uiState_.strengthA,
            uiState_.strengthB
        );
    }
}

} // namespace DGLab