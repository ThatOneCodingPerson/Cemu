#include "Cafe/HW/Latte/Core/LatteShaderCache.h"
#include "Cemu/FileCache/FileCache.h"
#include "config/ActiveSettings.h"
#include "JNIUtils.h"

// Per-game shader cache management for the app. The transferable caches (the game's shader and pipeline "lists")
// are hardware and driver independent; the core compiles them for the current driver at every boot (see
// docs/claude/FEATURE_RESEARCH.md section 8).
namespace NativeShaderCache
{
	// LatteShaderCache.cpp: shader files of Cemu 1.25.0 - 1.25.1b used a fixed version instead of a per-title one
	constexpr uint32 LEGACY_SHADER_CACHE_EXTRA_VERSION = 2;

	enum ImportResult : jint
	{
		IMPORTED = 0,
		NOT_A_CACHE_OF_THIS_TITLE = 1,
		IO_ERROR = 2,
	};

	enum CacheKind : jint
	{
		SHADERS = 0,
		PIPELINES = 1,
	};

	fs::path TransferablePath(uint64 titleId, CacheKind kind)
	{
		if (kind == PIPELINES)
			return ActiveSettings::GetCachePath("shaderCache/transferable/{:016x}_vkpipeline.bin", titleId);
		return ActiveSettings::GetCachePath("shaderCache/transferable/{:016x}_shaders.bin", titleId);
	}

	// Opens a cache file of this title the way the core accepts it (the header's extra version encodes the title),
	// read-only. allowLegacy: the legacy shader version isn't bound to a title.
	std::unique_ptr<FileCache> OpenForTitle(const fs::path& path, uint64 titleId, bool allowLegacy, CacheKind& kindOut)
	{
		if (FileCache* cache = FileCache::Open(path, false, LatteShaderCache_getShaderCacheExtraVersion(titleId)))
		{
			kindOut = SHADERS;
			return std::unique_ptr<FileCache>(cache);
		}
		if (FileCache* cache = FileCache::Open(path, false, LatteShaderCache_getPipelineCacheExtraVersion(titleId)))
		{
			kindOut = PIPELINES;
			return std::unique_ptr<FileCache>(cache);
		}
		if (allowLegacy)
		{
			if (FileCache* cache = FileCache::Open(path, false, LEGACY_SHADER_CACHE_EXTRA_VERSION))
			{
				kindOut = SHADERS;
				return std::unique_ptr<FileCache>(cache);
			}
		}
		return nullptr;
	}

	sint32 CountEntries(uint64 titleId, CacheKind kind)
	{
		const fs::path path = TransferablePath(titleId, kind);
		std::error_code ec;
		if (!fs::exists(path, ec))
			return -1;
		CacheKind openedKind;
		auto cache = OpenForTitle(path, titleId, kind == SHADERS, openedKind);
		return cache && openedKind == kind ? cache->GetFileCount() : -1;
	}

	// Adds the source's entries the title's cache doesn't have (entries are keyed by hash), so caches from several
	// sources combine into the most complete list.
	std::array<jint, 4> Import(uint64 titleId, const fs::path& sourcePath, bool allowLegacy)
	{
		CacheKind kind;
		auto source = OpenForTitle(sourcePath, titleId, allowLegacy, kind);
		if (!source)
			return {NOT_A_CACHE_OF_THIS_TITLE, 0, 0, 0};

		const fs::path destinationPath = TransferablePath(titleId, kind);
		std::error_code ec;
		fs::create_directories(destinationPath.parent_path(), ec);
		const uint32 extraVersion = kind == PIPELINES ? LatteShaderCache_getPipelineCacheExtraVersion(titleId)
													  : LatteShaderCache_getShaderCacheExtraVersion(titleId);
		// not FileCache::Open(allowCreate): an existing file with another version would be recreated (wiped)
		std::unique_ptr<FileCache> destination(FileCache::Open(destinationPath, false, extraVersion));
		if (!destination && kind == SHADERS)
			destination.reset(FileCache::Open(destinationPath, false, LEGACY_SHADER_CACHE_EXTRA_VERSION));
		if (!destination)
		{
			if (fs::exists(destinationPath, ec))
				return {IO_ERROR, kind, 0, 0}; // unreadable, leave it alone
			destination.reset(FileCache::Create(destinationPath, extraVersion));
			if (!destination)
				return {IO_ERROR, kind, 0, 0};
		}
		// entries are stored as the core stores them
		destination->UseCompression(false);

		jint addedCount = 0;
		std::vector<uint8> data;
		for (sint32 i = 0; i < source->GetMaximumFileIndex(); i++)
		{
			uint64 name1, name2;
			if (!source->GetFileByIndex(i, &name1, &name2, data))
				continue;
			if (destination->HasFile({name1, name2}))
				continue;
			destination->AddFile({name1, name2}, data.data(), (sint32)data.size());
			addedCount++;
		}
		return {IMPORTED, kind, addedCount, destination->GetFileCount()};
	}
} // namespace NativeShaderCache

// {shader entries, pipeline entries, SPIR-V cache bytes, driver cache bytes}; -1 = no (valid) file
extern "C" [[maybe_unused]] JNIEXPORT jlongArray JNICALL
Java_info_cemu_cemu_nativeinterface_NativeShaderCache_getCacheInfo(JNIEnv* env, [[maybe_unused]] jclass clazz, jlong titleId)
{
	std::array<jlong, 4> info{-1, -1, -1, -1};
	JNIUtils::HandleNativeException(env, [&]() {
		info[0] = NativeShaderCache::CountEntries(titleId, NativeShaderCache::SHADERS);
		info[1] = NativeShaderCache::CountEntries(titleId, NativeShaderCache::PIPELINES);
		std::error_code ec;
		const auto spirvSize = fs::file_size(ActiveSettings::GetCachePath("shaderCache/precompiled/{:016x}_spirv.bin", (uint64)titleId), ec);
		info[2] = ec ? -1 : (jlong)spirvSize;
		const auto driverSize = fs::file_size(ActiveSettings::GetCachePath("shaderCache/driver/vk/{:016x}.bin", (uint64)titleId), ec);
		info[3] = ec ? -1 : (jlong)driverSize;
	});
	jlongArray result = env->NewLongArray(info.size());
	if (result)
		env->SetLongArrayRegion(result, 0, info.size(), info.data());
	return result;
}

// {shaders file, pipelines file} of the title, for exporting; they may not exist
extern "C" [[maybe_unused]] JNIEXPORT jobjectArray JNICALL
Java_info_cemu_cemu_nativeinterface_NativeShaderCache_getTransferableCachePaths(JNIEnv* env, [[maybe_unused]] jclass clazz, jlong titleId)
{
	std::vector<std::string> paths{
		_pathToUtf8(NativeShaderCache::TransferablePath(titleId, NativeShaderCache::SHADERS)),
		_pathToUtf8(NativeShaderCache::TransferablePath(titleId, NativeShaderCache::PIPELINES)),
	};
	return JNIUtils::CreateStringObjectArray(env, paths);
}

// {result, kind, added entries, entries now}; see NativeShaderCache::ImportResult and CacheKind
extern "C" [[maybe_unused]] JNIEXPORT jintArray JNICALL
Java_info_cemu_cemu_nativeinterface_NativeShaderCache_importTransferableCache(JNIEnv* env, [[maybe_unused]] jclass clazz, jlong titleId, jstring sourcePath, jboolean allowLegacy)
{
	std::array<jint, 4> result{NativeShaderCache::IO_ERROR, 0, 0, 0};
	JNIUtils::HandleNativeException(env, [&]() {
		result = NativeShaderCache::Import(titleId, _utf8ToPath(JNIUtils::FromJString(env, sourcePath)), allowLegacy);
	});
	jintArray array = env->NewIntArray(result.size());
	if (array)
		env->SetIntArrayRegion(array, 0, result.size(), result.data());
	return array;
}
