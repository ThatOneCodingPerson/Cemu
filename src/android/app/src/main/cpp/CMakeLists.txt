add_library(CemuAndroid SHARED
        AndroidFilesystemCallbacks.cpp
        AndroidInputHelpers.cpp
        CompressTitleCallbacks.cpp
        GameTitleLoader.cpp
        Image.cpp
        JNIUtils.cpp
        NativeAccount.cpp
        NativeActiveSettings.cpp
        NativeEmulatedUSBDevices.cpp
        NativeEmulation.cpp
        NativeErrors.cpp
        NativeGameTitles.cpp
        NativeGraphicPacks.cpp
        NativeInput.cpp
        NativeLib.cpp
        NativeLocalization.cpp
        NativeLogging.cpp
        NativeSettings.cpp
        NativeShaderCache.cpp
        NativeSwkbd.cpp
        WuaConverter.cpp
)

cemu_use_precompiled_header(CemuAndroid)

target_link_libraries(CemuAndroid PRIVATE
        -landroid
        -llog
        CemuCommon
        CemuAudio
        CemuComponents
        CemuCafe
        CemuBin
        CemuGui
        ZArchive::zarchive
        stb
)
