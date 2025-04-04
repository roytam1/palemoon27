/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef GFX_WINDOWS_PLATFORM_H
#define GFX_WINDOWS_PLATFORM_H


/**
 * XXX to get CAIRO_HAS_D2D_SURFACE, CAIRO_HAS_DWRITE_FONT
 * and cairo_win32_scaled_font_select_font
 */
#include "cairo-win32.h"

#include "gfxCrashReporterUtils.h"
#include "gfxFontUtils.h"
#include "gfxWindowsSurface.h"
#include "gfxFont.h"
#include "gfxDWriteFonts.h"
#include "gfxPlatform.h"
#include "gfxTelemetry.h"
#include "gfxTypes.h"
#include "mozilla/Attributes.h"
#include "mozilla/Atomics.h"
#include "nsTArray.h"
#include "nsDataHashtable.h"

#include "mozilla/Mutex.h"
#include "mozilla/RefPtr.h"

#include <windows.h>
#include <objbase.h>

#include <dxgi.h>

// This header is available in the June 2010 SDK and in the Win8 SDK
#include <d3dcommon.h>
// Win 8.0 SDK types we'll need when building using older sdks.
#if !defined(D3D_FEATURE_LEVEL_11_1) // defined in the 8.0 SDK only
#define D3D_FEATURE_LEVEL_11_1 static_cast<D3D_FEATURE_LEVEL>(0xb100)
#define D3D_FL9_1_REQ_TEXTURE2D_U_OR_V_DIMENSION 2048
#define D3D_FL9_3_REQ_TEXTURE2D_U_OR_V_DIMENSION 4096
#endif

namespace mozilla {
namespace gfx {
class DrawTarget;
class FeatureState;
}
namespace layers {
class DeviceManagerD3D9;
class ReadbackManagerD3D11;
}
}
struct IDirect3DDevice9;
struct ID3D11Device;
struct IDXGIAdapter1;

/**
 * Utility to get a Windows HDC from a Moz2D DrawTarget.  If the DrawTarget is
 * not backed by a HDC this will get the HDC for the screen device context
 * instead.
 */
class MOZ_STACK_CLASS DCFromDrawTarget final
{
public:
    DCFromDrawTarget(mozilla::gfx::DrawTarget& aDrawTarget);

    ~DCFromDrawTarget() {
        if (mNeedsRelease) {
            ReleaseDC(nullptr, mDC);
        } else {
            RestoreDC(mDC, -1);
        }
    }

    operator HDC () {
        return mDC;
    }

private:
    HDC mDC;
    bool mNeedsRelease;
};

// ClearType parameters set by running ClearType tuner
struct ClearTypeParameterInfo {
    ClearTypeParameterInfo() :
        gamma(-1), pixelStructure(-1), clearTypeLevel(-1), enhancedContrast(-1)
    { }

    nsString    displayName;  // typically just 'DISPLAY1'
    int32_t     gamma;
    int32_t     pixelStructure;
    int32_t     clearTypeLevel;
    int32_t     enhancedContrast;
};

extern bool gANGLESupportsD3D11;

class gfxWindowsPlatform : public gfxPlatform {
public:
    enum TextRenderingMode {
        TEXT_RENDERING_NO_CLEARTYPE,
        TEXT_RENDERING_NORMAL,
        TEXT_RENDERING_GDI_CLASSIC,
        TEXT_RENDERING_COUNT
    };

    gfxWindowsPlatform();
    virtual ~gfxWindowsPlatform();
    static gfxWindowsPlatform *GetPlatform() {
        return (gfxWindowsPlatform*) gfxPlatform::GetPlatform();
    }

    virtual gfxPlatformFontList* CreatePlatformFontList() override;

    virtual already_AddRefed<gfxASurface>
      CreateOffscreenSurface(const IntSize& aSize,
                             gfxImageFormat aFormat) override;

    virtual already_AddRefed<mozilla::gfx::ScaledFont>
      GetScaledFontForFont(mozilla::gfx::DrawTarget* aTarget, gfxFont *aFont) override;

    enum RenderMode {
        /* Use GDI and windows surfaces */
        RENDER_GDI = 0,

        /* Use 32bpp image surfaces and call StretchDIBits */
        RENDER_IMAGE_STRETCH32,

        /* Use 32bpp image surfaces, and do 32->24 conversion before calling StretchDIBits */
        RENDER_IMAGE_STRETCH24,

        /* Use Direct2D rendering */
        RENDER_DIRECT2D,

        /* max */
        RENDER_MODE_MAX
    };

    bool IsDirect2DBackend();

    /**
     * Updates render mode with relation to the current preferences and
     * available devices.
     */
    void UpdateRenderMode();

    /**
     * Forces all GPU resources to be recreated on the next frame.
     */
    void ForceDeviceReset(ForcedDeviceResetReason aReason);

    /**
     * Verifies a D2D device is present and working, will attempt to create one
     * it is non-functional or non-existant.
     *
     * \param aAttemptForce Attempt to force D2D cairo device creation by using
     * cairo device creation routines.
     */
    void VerifyD2DDevice(bool aAttemptForce);

    virtual void GetCommonFallbackFonts(uint32_t aCh, uint32_t aNextCh,
                                        Script aRunScript,
                                        nsTArray<const char*>& aFontList) override;

    gfxFontGroup*
    CreateFontGroup(const mozilla::FontFamilyList& aFontFamilyList,
                    const gfxFontStyle *aStyle,
                    gfxTextPerfMetrics* aTextPerf,
                    gfxUserFontSet *aUserFontSet,
                    gfxFloat aDevToCssSize) override;

    virtual bool CanUseHardwareVideoDecoding() override;

    /**
     * Check whether format is supported on a platform or not (if unclear, returns true)
     */
    virtual bool IsFontFormatSupported(nsIURI *aFontURI, uint32_t aFormatFlags) override;

    bool DidRenderingDeviceReset(DeviceResetReason* aResetReason = nullptr) override;
    void SchedulePaintIfDeviceReset() override;
    void UpdateRenderModeIfDeviceReset() override;

    mozilla::gfx::BackendType GetContentBackendFor(mozilla::layers::LayersBackend aLayers) override;

    // ClearType is not always enabled even when available (e.g. Windows XP)
    // if either of these prefs are enabled and apply, use ClearType rendering
    bool UseClearTypeForDownloadableFonts();
    bool UseClearTypeAlways();

    static void GetDLLVersion(char16ptr_t aDLLPath, nsAString& aVersion);

    // returns ClearType tuning information for each display
    static void GetCleartypeParams(nsTArray<ClearTypeParameterInfo>& aParams);

    virtual void FontsPrefsChanged(const char *aPref) override;

    void SetupClearTypeParams();

    IDWriteFactory *GetDWriteFactory() { return mDWriteFactory; }
    inline bool DWriteEnabled() { return !!mDWriteFactory; }
    inline DWRITE_MEASURING_MODE DWriteMeasuringMode() { return mMeasuringMode; }

    IDWriteRenderingParams *GetRenderingParams(TextRenderingMode aRenderMode)
    { return mRenderingParams[aRenderMode]; }

    bool GetD3D11Device(RefPtr<ID3D11Device>* aOutDevice);
    bool GetD3D11DeviceForCurrentThread(RefPtr<ID3D11Device>* aOutDevice);
    bool GetD3D11ImageBridgeDevice(RefPtr<ID3D11Device>* aOutDevice);

    void OnDeviceManagerDestroy(mozilla::layers::DeviceManagerD3D9* aDeviceManager);
    already_AddRefed<mozilla::layers::DeviceManagerD3D9> GetD3D9DeviceManager();
    IDirect3DDevice9* GetD3D9Device();
    void D3D9DeviceReset();
    ID3D11Device *GetD3D11ContentDevice();

    // Create a D3D11 device to be used for DXVA decoding.
    already_AddRefed<ID3D11Device> CreateD3D11DecoderDevice();
    bool CreateD3D11DecoderDeviceHelper(
      IDXGIAdapter1* aAdapter, RefPtr<ID3D11Device>& aDevice,
      HRESULT& aResOut);

    bool DwmCompositionEnabled();

    mozilla::layers::ReadbackManagerD3D11* GetReadbackManager();

    static bool IsOptimus();

    bool IsWARP() const { return mIsWARP; }

    // Returns whether the compositor's D3D11 device supports texture sharing.
    bool CompositorD3D11TextureSharingWorks() const {
      return mCompositorD3D11TextureSharingWorks;
    }

    bool SupportsApzWheelInput() const override {
      return true;
    }
    bool SupportsApzTouchInput() const override;

    // Recreate devices as needed for a device reset. Returns true if a device
    // reset occurred.
    bool HandleDeviceReset();
    void UpdateBackendPrefs();

    unsigned GetD3D11Version();

    void TestDeviceReset(DeviceResetReason aReason);

    virtual already_AddRefed<mozilla::gfx::VsyncSource> CreateHardwareVsyncSource() override;
    static mozilla::Atomic<size_t> sD3D11MemoryUsed;
    static mozilla::Atomic<size_t> sD3D9MemoryUsed;
    static mozilla::Atomic<size_t> sD3D9SharedTextureUsed;

    void GetDeviceInitData(mozilla::gfx::DeviceInitData* aOut) override;

    bool SupportsPluginDirectBitmapDrawing() override {
      return true;
    }
    bool SupportsPluginDirectDXGIDrawing();

    virtual bool CanUseDirect3D11ANGLE();

protected:
    bool AccelerateLayersByDefault() override {
      return true;
    }
    void GetAcceleratedCompositorBackends(nsTArray<mozilla::layers::LayersBackend>& aBackends) override;
    virtual void GetPlatformCMSOutputProfile(void* &mem, size_t &size) override;
    bool UpdateDeviceInitData() override;

protected:
    RenderMode mRenderMode;

    int8_t mUseClearTypeForDownloadableFonts;
    int8_t mUseClearTypeAlways;

private:
    void Init();
    void InitAcceleration() override;

    void InitializeDevices();
    void InitializeD3D11();
    void InitializeD2D();
    bool InitDWriteSupport();

    void DisableD2D(mozilla::gfx::FeatureStatus aStatus, const char* aMessage);

    void InitializeConfig();
    void InitializeD3D9Config();
    void InitializeD3D11Config();
    void InitializeD2DConfig();

    void AttemptD3D11DeviceCreation(mozilla::gfx::FeatureState& d3d11);
    bool AttemptD3D11DeviceCreationHelper(
        IDXGIAdapter1* aAdapter,
        RefPtr<ID3D11Device>& aOutDevice,
        HRESULT& aResOut);

    void AttemptWARPDeviceCreation();
    bool AttemptWARPDeviceCreationHelper(
        mozilla::ScopedGfxFeatureReporter& aReporterWARP,
        RefPtr<ID3D11Device>& aOutDevice,
        HRESULT& aResOut);

    bool AttemptD3D11ImageBridgeDeviceCreationHelper(
        IDXGIAdapter1* aAdapter, HRESULT& aResOut);
    mozilla::gfx::FeatureStatus AttemptD3D11ImageBridgeDeviceCreation();

    mozilla::gfx::FeatureStatus AttemptD3D11ContentDeviceCreation();
    bool AttemptD3D11ContentDeviceCreationHelper(
        IDXGIAdapter1* aAdapter, HRESULT& aResOut);

    bool CanUseWARP();
    bool CanUseD3D11ImageBridge();
    bool ContentAdapterIsParentAdapter(ID3D11Device* device);

    void DisableD3D11AfterCrash();
    void ResetD3D11Devices();

    IDXGIAdapter1 *GetDXGIAdapter();
    bool IsDeviceReset(HRESULT hr, DeviceResetReason* aReason);

    RefPtr<IDWriteFactory> mDWriteFactory;
    RefPtr<IDWriteRenderingParams> mRenderingParams[TEXT_RENDERING_COUNT];
    DWRITE_MEASURING_MODE mMeasuringMode;

    mozilla::Mutex mDeviceLock;
    RefPtr<IDXGIAdapter1> mAdapter;
    RefPtr<ID3D11Device> mD3D11Device;
    RefPtr<ID3D11Device> mD3D11ContentDevice;
    RefPtr<ID3D11Device> mD3D11ImageBridgeDevice;
    RefPtr<mozilla::layers::DeviceManagerD3D9> mDeviceManager;
    mozilla::Atomic<bool> mIsWARP;
    bool mHasDeviceReset;
    bool mHasFakeDeviceReset;
    mozilla::Atomic<bool> mCompositorD3D11TextureSharingWorks;
    mozilla::Atomic<bool> mHasD3D9DeviceReset;
    DeviceResetReason mDeviceResetReason;

    RefPtr<mozilla::layers::ReadbackManagerD3D11> mD3D11ReadbackManager;

    nsTArray<D3D_FEATURE_LEVEL> mFeatureLevels;
};

#endif /* GFX_WINDOWS_PLATFORM_H */
