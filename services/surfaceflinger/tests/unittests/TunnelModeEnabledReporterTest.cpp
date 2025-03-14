/*
 * Copyright 2021 The Android Open Source Project
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

#undef LOG_TAG
#define LOG_TAG "TunnelModeEnabledReporterTest"

#include <android/gui/BnTunnelModeEnabledListener.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <gui/LayerMetadata.h>

#include "TestableSurfaceFlinger.h"
#include "TunnelModeEnabledReporter.h"
#include "mock/DisplayHardware/MockComposer.h"

namespace android {

using testing::_;
using testing::Mock;
using testing::Return;

using android::Hwc2::IComposer;
using android::Hwc2::IComposerClient;

constexpr int DEFAULT_SIDEBAND_STREAM = 51;

struct TestableTunnelModeEnabledListener : public gui::BnTunnelModeEnabledListener {
    TestableTunnelModeEnabledListener() {}

    bool mTunnelModeEnabled = false;

    binder::Status onTunnelModeEnabledChanged(bool tunnelModeEnabled) override {
        mTunnelModeEnabled = tunnelModeEnabled;
        return binder::Status::ok();
    }
};

class TunnelModeEnabledReporterTest : public testing::Test {
public:
    TunnelModeEnabledReporterTest();
    ~TunnelModeEnabledReporterTest() override;

protected:
    static constexpr uint32_t WIDTH = 100;
    static constexpr uint32_t HEIGHT = 100;
    static constexpr uint32_t LAYER_FLAGS = 0;

    sp<Layer> createBufferStateLayer(LayerMetadata metadata);

    TestableSurfaceFlinger mFlinger;
    Hwc2::mock::Composer* mComposer = nullptr;

    sp<TestableTunnelModeEnabledListener> mTunnelModeEnabledListener =
            sp<TestableTunnelModeEnabledListener>::make();

    sp<TunnelModeEnabledReporter> mTunnelModeEnabledReporter =
            sp<TunnelModeEnabledReporter>::make();
};

TunnelModeEnabledReporterTest::TunnelModeEnabledReporterTest() {
    const ::testing::TestInfo* const test_info =
            ::testing::UnitTest::GetInstance()->current_test_info();
    ALOGD("**** Setting up for %s.%s\n", test_info->test_case_name(), test_info->name());

    mFlinger.setupMockScheduler();
    mFlinger.setupComposer(std::make_unique<Hwc2::mock::Composer>());
    mFlinger.flinger()->mTunnelModeEnabledReporter = mTunnelModeEnabledReporter;
    mTunnelModeEnabledReporter->dispatchTunnelModeEnabled(false);
}

TunnelModeEnabledReporterTest::~TunnelModeEnabledReporterTest() {
    const ::testing::TestInfo* const test_info =
            ::testing::UnitTest::GetInstance()->current_test_info();
    ALOGD("**** Tearing down after %s.%s\n", test_info->test_case_name(), test_info->name());
    mTunnelModeEnabledReporter->dispatchTunnelModeEnabled(false);
    mTunnelModeEnabledReporter->removeListener(mTunnelModeEnabledListener);
}

sp<Layer> TunnelModeEnabledReporterTest::createBufferStateLayer(LayerMetadata metadata = {}) {
    sp<Client> client;
    LayerCreationArgs args(mFlinger.flinger(), client, "buffer-state-layer", LAYER_FLAGS, metadata);
    return sp<Layer>::make(args);
}

namespace {

TEST_F(TunnelModeEnabledReporterTest, callsAddedListeners) {
    mTunnelModeEnabledReporter->addListener(mTunnelModeEnabledListener);

    bool expectedTunnelModeEnabled = false;
    mTunnelModeEnabledReporter->dispatchTunnelModeEnabled(expectedTunnelModeEnabled);
    EXPECT_EQ(expectedTunnelModeEnabled, mTunnelModeEnabledListener->mTunnelModeEnabled);

    expectedTunnelModeEnabled = true;
    mTunnelModeEnabledReporter->dispatchTunnelModeEnabled(expectedTunnelModeEnabled);
    EXPECT_EQ(expectedTunnelModeEnabled, mTunnelModeEnabledListener->mTunnelModeEnabled);

    mTunnelModeEnabledReporter->removeListener(mTunnelModeEnabledListener);

    mTunnelModeEnabledReporter->dispatchTunnelModeEnabled(false);
    EXPECT_EQ(expectedTunnelModeEnabled, mTunnelModeEnabledListener->mTunnelModeEnabled);
}

TEST_F(TunnelModeEnabledReporterTest, callsNewListenerImmediately) {
    bool expectedTunnelModeEnabled = false;
    mTunnelModeEnabledReporter->dispatchTunnelModeEnabled(expectedTunnelModeEnabled);

    mTunnelModeEnabledReporter->addListener(mTunnelModeEnabledListener);
    EXPECT_EQ(expectedTunnelModeEnabled, mTunnelModeEnabledListener->mTunnelModeEnabled);
}

TEST_F(TunnelModeEnabledReporterTest, callsNewListenerWithFreshInformation) {
    sp<Layer> layer = createBufferStateLayer();
    sp<NativeHandle> stream =
            NativeHandle::create(reinterpret_cast<native_handle_t*>(DEFAULT_SIDEBAND_STREAM),
                                 false);
    layer->setSidebandStream(stream, FrameTimelineInfo{}, 20, gui::GameMode::Unsupported);
    mTunnelModeEnabledReporter->updateTunnelModeStatus();
    mTunnelModeEnabledReporter->addListener(mTunnelModeEnabledListener);
    EXPECT_EQ(true, mTunnelModeEnabledListener->mTunnelModeEnabled);
    mTunnelModeEnabledReporter->removeListener(mTunnelModeEnabledListener);
    layer = nullptr;

    mTunnelModeEnabledReporter->updateTunnelModeStatus();
    mTunnelModeEnabledReporter->addListener(mTunnelModeEnabledListener);

    EXPECT_EQ(false, mTunnelModeEnabledListener->mTunnelModeEnabled);
}

TEST_F(TunnelModeEnabledReporterTest, layerWithSidebandStreamTriggersUpdate) {
    mTunnelModeEnabledReporter->addListener(mTunnelModeEnabledListener);
    EXPECT_EQ(false, mTunnelModeEnabledListener->mTunnelModeEnabled);

    sp<Layer> simpleLayer = createBufferStateLayer();
    sp<Layer> layerWithSidebandStream = createBufferStateLayer();
    sp<NativeHandle> stream =
            NativeHandle::create(reinterpret_cast<native_handle_t*>(DEFAULT_SIDEBAND_STREAM),
                                 false);
    layerWithSidebandStream->setSidebandStream(stream, FrameTimelineInfo{}, 20,
                                               gui::GameMode::Unsupported);

    mTunnelModeEnabledReporter->updateTunnelModeStatus();
    EXPECT_EQ(true, mTunnelModeEnabledListener->mTunnelModeEnabled);

    layerWithSidebandStream = nullptr;
    mTunnelModeEnabledReporter->updateTunnelModeStatus();
    EXPECT_EQ(false, mTunnelModeEnabledListener->mTunnelModeEnabled);
}

} // namespace
} // namespace android
