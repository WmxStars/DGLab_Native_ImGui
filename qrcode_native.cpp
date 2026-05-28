#include "qrcode_native.h"
#include "qrcodegen.hpp"
#include <fstream>
#include <algorithm>

namespace DGLab {

std::string QRCodeNative::BuildDGLabSocketUrl(const std::string& host, int port, const std::string& clientId) {
    // App 扫码就知道要连 ws:// 啦
    return "https://www.dungeon-lab.com/app-download.php#DGLAB-SOCKET#ws://" +
           host + ":" + std::to_string(port) + "/" + clientId;
}

QRCodeNative::Bitmap QRCodeNative::GenerateBitmap(const std::string& text, int scale, int border) {
    Bitmap bitmap;
    if (text.empty()) return bitmap;
    if (scale < 1) scale = 1;
    if (border < 0) border = 0;

    using qrcodegen::QrCode;
    QrCode qr = QrCode::encodeText(text.c_str(), QrCode::Ecc::HIGH);

    const int qrSize = qr.getSize();
    const int outSize = (qrSize + border * 2) * scale;
    bitmap.width = outSize;
    bitmap.height = outSize;
    bitmap.rgba.assign(outSize * outSize * 4, 255);

    // 先铺白底，再画黑块喵
    for (int y = 0; y < outSize; ++y) {
        for (int x = 0; x < outSize; ++x) {
            int idx = (y * outSize + x) * 4;
            bitmap.rgba[idx + 0] = 255;
            bitmap.rgba[idx + 1] = 255;
            bitmap.rgba[idx + 2] = 255;
            bitmap.rgba[idx + 3] = 255;
        }
    }

    for (int y = 0; y < qrSize; ++y) {
        for (int x = 0; x < qrSize; ++x) {
            if (!qr.getModule(x, y)) continue;
            int startX = (x + border) * scale;
            int startY = (y + border) * scale;
            for (int py = 0; py < scale; ++py) {
                for (int px = 0; px < scale; ++px) {
                    int outX = startX + px;
                    int outY = startY + py;
                    int idx = (outY * outSize + outX) * 4;
                    bitmap.rgba[idx + 0] = 0;
                    bitmap.rgba[idx + 1] = 0;
                    bitmap.rgba[idx + 2] = 0;
                    bitmap.rgba[idx + 3] = 255;
                }
            }
        }
    }

    return bitmap;
}

bool QRCodeNative::SavePPM(const Bitmap& bitmap, const std::string& path) {
    if (bitmap.empty()) return false;
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;

    out << "P6\n" << bitmap.width << " " << bitmap.height << "\n255\n";
    for (int i = 0; i < bitmap.width * bitmap.height; ++i) {
        out.put((char)bitmap.rgba[i * 4 + 0]);
        out.put((char)bitmap.rgba[i * 4 + 1]);
        out.put((char)bitmap.rgba[i * 4 + 2]);
    }
    return true;
}

void QRCodeNative::DrawQRCode(ImDrawList* drawList, ImVec2 topLeft, float size, const std::string& text, ImU32 dark, ImU32 light) {
    if (drawList == nullptr || text.empty() || size <= 1.0f) return;

    using qrcodegen::QrCode;
    QrCode qr = QrCode::encodeText(text.c_str(), QrCode::Ecc::HIGH);

    const int qrSize = qr.getSize();
    const int border = 4;
    const int fullCells = qrSize + border * 2;
    const float cell = size / (float)fullCells;

    // 白底
    drawList->AddRectFilled(topLeft, ImVec2(topLeft.x + size, topLeft.y + size), light, 6.0f);

    // 黑块
    for (int y = 0; y < qrSize; ++y) {
        for (int x = 0; x < qrSize; ++x) {
            if (!qr.getModule(x, y)) continue;
            float x0 = topLeft.x + (x + border) * cell;
            float y0 = topLeft.y + (y + border) * cell;
            float x1 = topLeft.x + (x + border + 1) * cell + 0.5f;
            float y1 = topLeft.y + (y + border + 1) * cell + 0.5f;
            drawList->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), dark);
        }
    }
}

} // namespace DGLab