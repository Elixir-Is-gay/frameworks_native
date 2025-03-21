/*
 * Copyright (C) 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <gui/AidlUtil.h>
#include <gui/SyncScreenCaptureListener.h>
#include <private/gui/ComposerServiceAIDL.h>
#include <ui/FenceResult.h>
#include <ui/Rect.h>
#include <utils/String8.h>
#include <functional>
#include "TransactionUtils.h"

namespace android {

using gui::aidl_utils::statusTFromBinderStatus;

namespace {

// A ScreenCapture is a screenshot from SurfaceFlinger that can be used to check
// individual pixel values for testing purposes.
class ScreenCapture : public RefBase {
public:
    static status_t captureDisplay(DisplayCaptureArgs& captureArgs,
                                   ScreenCaptureResults& captureResults) {
        const auto sf = ComposerServiceAIDL::getComposerService();
        SurfaceComposerClient::Transaction().apply(true);

        captureArgs.captureArgs.dataspace = static_cast<int32_t>(ui::Dataspace::V0_SRGB);
        const sp<SyncScreenCaptureListener> captureListener = sp<SyncScreenCaptureListener>::make();
        binder::Status status = sf->captureDisplay(captureArgs, captureListener);
        status_t err = statusTFromBinderStatus(status);
        if (err != NO_ERROR) {
            return err;
        }
        captureResults = captureListener->waitForResults();
        return fenceStatus(captureResults.fenceResult);
    }

    static void captureScreen(std::unique_ptr<ScreenCapture>* sc) {
        const auto ids = SurfaceComposerClient::getPhysicalDisplayIds();
        // TODO(b/248317436): extend to cover all displays for multi-display devices
        const auto display =
                ids.empty() ? nullptr : SurfaceComposerClient::getPhysicalDisplayToken(ids.front());
        captureScreen(sc, display);
    }

    static void captureScreen(std::unique_ptr<ScreenCapture>* sc, sp<IBinder> displayToken) {
        DisplayCaptureArgs args;
        args.displayToken = displayToken;
        captureDisplay(sc, args);
    }

    static void captureDisplay(std::unique_ptr<ScreenCapture>* sc,
                               DisplayCaptureArgs& captureArgs) {
        ScreenCaptureResults captureResults;
        ASSERT_EQ(NO_ERROR, captureDisplay(captureArgs, captureResults));
        *sc = std::make_unique<ScreenCapture>(captureResults.buffer,
                                              captureResults.capturedHdrLayers);
    }

    static status_t captureLayers(LayerCaptureArgs& captureArgs,
                                  ScreenCaptureResults& captureResults) {
        const auto sf = ComposerServiceAIDL::getComposerService();
        SurfaceComposerClient::Transaction().apply(true);

        captureArgs.captureArgs.dataspace = static_cast<int32_t>(ui::Dataspace::V0_SRGB);
        const sp<SyncScreenCaptureListener> captureListener = sp<SyncScreenCaptureListener>::make();
        binder::Status status = sf->captureLayers(captureArgs, captureListener);
        status_t err = statusTFromBinderStatus(status);
        if (err != NO_ERROR) {
            return err;
        }
        captureResults = captureListener->waitForResults();
        return fenceStatus(captureResults.fenceResult);
    }

    static void captureLayers(std::unique_ptr<ScreenCapture>* sc, LayerCaptureArgs& captureArgs) {
        ScreenCaptureResults captureResults;
        ASSERT_EQ(NO_ERROR, captureLayers(captureArgs, captureResults));
        *sc = std::make_unique<ScreenCapture>(captureResults.buffer,
                                              captureResults.capturedHdrLayers);
    }

    bool capturedHdrLayers() const { return mContainsHdr; }

    void expectColor(const Rect& rect, const Color& color, uint8_t tolerance = 0) {
        ASSERT_NE(nullptr, mOutBuffer);
        ASSERT_NE(nullptr, mPixels);
        ASSERT_EQ(HAL_PIXEL_FORMAT_RGBA_8888, mOutBuffer->getPixelFormat());
        TransactionUtils::expectBufferColor(mOutBuffer, mPixels, rect, color, tolerance);
    }

    void expectBorder(const Rect& rect, const Color& color, uint8_t tolerance = 0) {
        ASSERT_NE(nullptr, mOutBuffer);
        ASSERT_EQ(HAL_PIXEL_FORMAT_RGBA_8888, mOutBuffer->getPixelFormat());
        const bool leftBorder = rect.left > 0;
        const bool topBorder = rect.top > 0;
        const bool rightBorder = rect.right < int32_t(mOutBuffer->getWidth());
        const bool bottomBorder = rect.bottom < int32_t(mOutBuffer->getHeight());

        if (topBorder) {
            Rect top(rect.left, rect.top - 1, rect.right, rect.top);
            if (leftBorder) {
                top.left -= 1;
            }
            if (rightBorder) {
                top.right += 1;
            }
            expectColor(top, color, tolerance);
        }
        if (leftBorder) {
            Rect left(rect.left - 1, rect.top, rect.left, rect.bottom);
            expectColor(left, color, tolerance);
        }
        if (rightBorder) {
            Rect right(rect.right, rect.top, rect.right + 1, rect.bottom);
            expectColor(right, color, tolerance);
        }
        if (bottomBorder) {
            Rect bottom(rect.left, rect.bottom, rect.right, rect.bottom + 1);
            if (leftBorder) {
                bottom.left -= 1;
            }
            if (rightBorder) {
                bottom.right += 1;
            }
            expectColor(bottom, color, tolerance);
        }
    }

    void expectQuadrant(const Rect& rect, const Color& topLeft, const Color& topRight,
                        const Color& bottomLeft, const Color& bottomRight, bool filtered = false,
                        uint8_t tolerance = 0) {
        ASSERT_TRUE((rect.right - rect.left) % 2 == 0 && (rect.bottom - rect.top) % 2 == 0);

        const int32_t centerX = rect.left + (rect.right - rect.left) / 2;
        const int32_t centerY = rect.top + (rect.bottom - rect.top) / 2;
        // avoid checking borders due to unspecified filtering behavior
        const int32_t offsetX = filtered ? 2 : 0;
        const int32_t offsetY = filtered ? 2 : 0;
        expectColor(Rect(rect.left, rect.top, centerX - offsetX, centerY - offsetY), topLeft,
                    tolerance);
        expectColor(Rect(centerX + offsetX, rect.top, rect.right, centerY - offsetY), topRight,
                    tolerance);
        expectColor(Rect(rect.left, centerY + offsetY, centerX - offsetX, rect.bottom), bottomLeft,
                    tolerance);
        expectColor(Rect(centerX + offsetX, centerY + offsetY, rect.right, rect.bottom),
                    bottomRight, tolerance);
    }

    void checkPixel(uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b) {
        ASSERT_NE(nullptr, mOutBuffer);
        ASSERT_EQ(HAL_PIXEL_FORMAT_RGBA_8888, mOutBuffer->getPixelFormat());
        const uint8_t* pixel = mPixels + (4 * (y * mOutBuffer->getStride() + x));
        if (r != pixel[0] || g != pixel[1] || b != pixel[2]) {
            String8 err(String8::format("pixel @ (%3d, %3d): "
                                        "expected [%3d, %3d, %3d], got [%3d, %3d, %3d]",
                                        x, y, r, g, b, pixel[0], pixel[1], pixel[2]));
            EXPECT_EQ(String8(), err) << err.c_str();
        }
    }

    Color getPixelColor(uint32_t x, uint32_t y) {
        if (!mOutBuffer || mOutBuffer->getPixelFormat() != HAL_PIXEL_FORMAT_RGBA_8888) {
            return {0, 0, 0, 0};
        }

        const uint8_t* pixel = mPixels + (4 * (y * mOutBuffer->getStride() + x));
        return {pixel[0], pixel[1], pixel[2], pixel[3]};
    }

    void expectFGColor(uint32_t x, uint32_t y) { checkPixel(x, y, 195, 63, 63); }

    void expectBGColor(uint32_t x, uint32_t y) { checkPixel(x, y, 63, 63, 195); }

    void expectChildColor(uint32_t x, uint32_t y) { checkPixel(x, y, 200, 200, 200); }

    void expectSize(uint32_t width, uint32_t height) {
        EXPECT_EQ(width, mOutBuffer->getWidth());
        EXPECT_EQ(height, mOutBuffer->getHeight());
    }

    explicit ScreenCapture(const sp<GraphicBuffer>& outBuffer, bool containsHdr)
          : mOutBuffer(outBuffer), mContainsHdr(containsHdr) {
        if (mOutBuffer) {
            mOutBuffer->lock(GRALLOC_USAGE_SW_READ_OFTEN, reinterpret_cast<void**>(&mPixels));
        }
    }

    ~ScreenCapture() {
        if (mOutBuffer) mOutBuffer->unlock();
    }

private:
    sp<GraphicBuffer> mOutBuffer;
    bool mContainsHdr = mContainsHdr;
    uint8_t* mPixels = nullptr;
};
} // namespace
} // namespace android
