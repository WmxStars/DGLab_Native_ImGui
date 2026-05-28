// ============================================================
//  game_example.cpp —— 游戏对接示例
//
//  演示如何用 GameHook 监控游戏事件并触发 DGLab 刺激
//  两种模式：内存读取 / 像素找色
// ============================================================

#include "game_hook.h"
#include "websocket_server.h"
#include "dglab_protocol.h"
#include <android/log.h>

#define LOG_TAG "DGLab-Example"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace DGLab {

// ============================================================
//  方式一：内存读取模式（需要 root）
//
//  适用于：能拿到游戏进程内存地址的场景
//  优点：精度高、延迟低、不受画面遮挡影响
//  缺点：需要 root、需要逆向找地址
// ============================================================

// 你的 MemoryReader 实现（需要自己对接 ptrace / /proc/pid/mem）
class MyMemoryReader : public MemoryReader {
public:
    bool attach(const std::string& processName) override {
        // TODO: 用 /proc/xxx/maps 找到目标进程
        // pid_ = findProcess(processName);
        // 读取 /proc/pid/maps 获取模块基地址
        LOGI("Attached to: %s", processName.c_str());
        return true;
    }
    
    void detach() override {
        // TODO: 清理资源
    }
    
    uintptr_t getModuleBase(const std::string& moduleName) override {
        // TODO: 解析 /proc/pid/maps 找到模块基地址
        // 例：libil2cpp.so 加载在 0x7200000000
        return 0;  // 需要实现
    }
    
    bool readInt32(uintptr_t address, int32_t& value) override {
        // TODO: 通过 /proc/pid/mem 读取
        // pread64(fd_, &value, 4, address);
        return false;  // 需要实现
    }
    
    bool readFloat(uintptr_t address, float& value) override {
        int32_t raw;
        if (!readInt32(address, raw)) return false;
        value = *reinterpret_cast<float*>(&raw);
        return true;
    }
    
    bool readInt16(uintptr_t address, int16_t& value) override {
        // TODO: pread64(fd_, &value, 2, address);
        return false;
    }
};

// ============================================================
//  方式二：像素找色模式（免 root）
//
//  适用于：无法读取内存但能看到游戏画面的场景
//  优点：免 root、通用性强
//  缺点：受分辨率、UI布局、画面遮挡影响
// ============================================================

// 你的 ScreenCapture 实现（对接截图 API）
class MyScreenCapture : public ScreenCapture {
public:
    int getWidth() const override { return 1080; }
    int getHeight() const override { return 2400; }
    
    bool captureRegion(int x, int y, int w, int h,
                       std::vector<uint8_t>& rgba) override {
        // TODO: 用 SurfaceFlinger / screencap / MediaProjection 截图
        // 方案1: screencap -p /tmp/screen.png 然后解码
        // 方案2: SurfaceControl::screenshot (需要系统权限)
        // 方案3: MediaProjection API (需要用户授权)
        return false;  // 需要实现
    }
    
    bool getPixel(int x, int y, uint8_t& r, uint8_t& g,
                  uint8_t& b, uint8_t& a) override {
        std::vector<uint8_t> rgba;
        if (!captureRegion(x, y, 1, 1, rgba)) return false;
        if (rgba.size() < 4) return false;
        r = rgba[0]; g = rgba[1]; b = rgba[2]; a = rgba[3];
        return true;
    }
};

// ============================================================
//  使用示例
// ============================================================

class GameIntegration {
public:
    void setupMemoryMode(WebSocketServer& ws) {
        LOGI("=== 内存读取模式 ===");
        
        auto reader = std::make_shared<MyMemoryReader>();
        reader->attach("com.example.game");
        
        hook_.setMemoryReader(reader);
        hook_.setMaxHp(100);
        
        // 设置血量内存地址
        // 假设血量在 libil2cpp.so + 0x1A2B3C 处，类型为 int32
        MemoryTarget hpTarget;
        hpTarget.moduleName = "libil2cpp.so";
        hpTarget.moduleOffset = 0x1A2B3C;
        hpTarget.valueType = 0;  // int32
        hpTarget.label = "HP";
        hook_.setHpMemoryTarget(hpTarget);
        
        // 注册规则
        hook_.addHpMonitor(
            5,      // 血量减少5以上才触发（避免微小变化频繁触发）
            500,    // 500ms冷却
            // 受伤回调：血量减少时触发轻微电击
            [&ws](int currentHp, int lost) {
                LOGI("HP lost: %d (current: %d)", lost, currentHp);
                
                if (!ws.isBound()) return;
                
                // 按失去血量比例计算强度（最多30）
                int strength = std::min(30, lost * 3);
                
                // 发送强度命令（轻微电击）
                std::string msg = "{\"type\":\"msg\","
                    "\"message\":\"strength-1+2+" + std::to_string(strength) + "\","
                    "\"clientId\":\"" + std::string(Protocol::FIXED_CLIENT_ID) + "\","
                    "\"targetId\":\"" + ws.getTargetId() + "\"}";
                ws.sendMessage(msg);
            },
            // 死亡回调：血量归零时触发预设波形
            [&ws]() {
                LOGI("HP = 0! Trigger death waveform!");
                
                if (!ws.isBound()) return;
                
                // 清空通道
                std::string prefix = "{\"type\":\"msg\","
                    "\"clientId\":\"" + std::string(Protocol::FIXED_CLIENT_ID) + "\","
                    "\"targetId\":\"" + ws.getTargetId() + "\",\"message\":\"";
                
                ws.sendMessage(prefix + "clear-1\"}");
                ws.sendMessage(prefix + "clear-2\"}");
                
                // 发送死亡波形（强度拉满80）
                // TODO: 这里需要加载 waveforms/ 里的波形并发送 pulse 命令
                // 简化版：直接用高强度 + 固定波形名
                ws.sendMessage(prefix + "strength-1+2+80\"}");
                ws.sendMessage(prefix + "strength-2+2+80\"}");
                
                LOGI("Death effect sent: strength=80");
            }
        );
        
        hook_.start();
        LOGI("Memory mode started");
    }
    
    void setupPixelMode(WebSocketServer& ws) {
        LOGI("=== 像素找色模式 ===");
        
        auto capture = std::make_shared<MyScreenCapture>();
        hook_.setScreenCapture(capture);
        hook_.setMaxHp(100);
        
        // 设置血条检测区域
        // 假设血条在屏幕顶部 (100, 50) 位置，宽 400px，高 20px
        // 血条颜色是红色 (220, 50, 50)
        hook_.setHpPixelRegion(
            100, 50,     // 血条左上角坐标
            400, 20,     // 血条宽高
            220, 50, 50, // 血条颜色 RGB
            40           // 颜色容差
        );
        
        // 同样注册规则
        hook_.addHpMonitor(
            5, 500,
            [&ws](int currentHp, int lost) {
                LOGI("HP lost (pixel): %d (current: %d)", lost, currentHp);
                
                if (!ws.isBound()) return;
                
                int strength = std::min(30, lost * 3);
                std::string msg = "{\"type\":\"msg\","
                    "\"message\":\"strength-1+2+" + std::to_string(strength) + "\","
                    "\"clientId\":\"" + std::string(Protocol::FIXED_CLIENT_ID) + "\","
                    "\"targetId\":\"" + ws.getTargetId() + "\"}";
                ws.sendMessage(msg);
            },
            [&ws]() {
                LOGI("DEAD (pixel)!");
                if (!ws.isBound()) return;
                
                std::string prefix = "{\"type\":\"msg\","
                    "\"clientId\":\"" + std::string(Protocol::FIXED_CLIENT_ID) + "\","
                    "\"targetId\":\"" + ws.getTargetId() + "\",\"message\":\"";
                ws.sendMessage(prefix + "clear-1\"}");
                ws.sendMessage(prefix + "clear-2\"}");
                ws.sendMessage(prefix + "strength-1+2+80\"}");
                ws.sendMessage(prefix + "strength-2+2+80\"}");
            }
        );
        
        hook_.start();
        LOGI("Pixel mode started");
    }
    
    void stop() {
        hook_.stop();
    }
    
private:
    GameHook hook_;
};

} // namespace DGLab