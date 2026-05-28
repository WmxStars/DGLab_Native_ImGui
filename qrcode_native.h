#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <imgui.h>

namespace DGLab {

// 🐾 Native二维码小工具：负责生成 DGLab App 扫码用的连接二维码喵
class QRCodeNative {
public:
    struct Bitmap {
        int width = 0;
        int height = 0;
        // RGBA8888 像素，小黑块和小白块都放在这里喵
        std::vector<unsigned char> rgba;
        bool empty() const { return width <= 0 || height <= 0 || rgba.empty(); }
    };

    // 生成 DGLab-Craft 同款扫码 URL：
    // https://www.dungeon-lab.com/app-download.php#DGLAB-SOCKET#ws://IP:PORT/FIXED_CLIENT_ID
    static std::string BuildDGLabSocketUrl(const std::string& host, int port, const std::string& clientId);

    // 生成二维码位图，scale 是每个二维码格子的像素大小，border 是白边格子数
    static Bitmap GenerateBitmap(const std::string& text, int scale = 8, int border = 4);

    // 写成非常简单的 PPM 图片，方便调试；Android/ImGui显示不用依赖这个
    static bool SavePPM(const Bitmap& bitmap, const std::string& path);

    // 绘制二维码到 ImGui DrawList，不依赖纹理，最稳喵
    static void DrawQRCode(ImDrawList* drawList, ImVec2 topLeft, float size, const std::string& text,
                           ImU32 dark = IM_COL32(0, 0, 0, 255),
                           ImU32 light = IM_COL32(255, 255, 255, 255));
};

} // namespace DGLab
