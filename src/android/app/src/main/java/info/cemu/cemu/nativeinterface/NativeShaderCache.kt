package info.cemu.cemu.nativeinterface

/**
 * A game's shader caches. The transferable files (`<titleid>_shaders.bin`, `<titleid>_vkpipeline.bin`) are the
 * game's shader and pipeline list; they work on any device and driver. The core compiles them for the current GPU
 * driver at every boot (SPIR-V and driver caches).
 */
object NativeShaderCache {
    /** Result of [getCacheInfo]; counts and sizes are -1 without a (valid) file. */
    data class CacheInfo(
        val shaderCount: Long,
        val pipelineCount: Long,
        val spirvCacheBytes: Long,
        val driverCacheBytes: Long,
    )

    object ImportResult {
        const val IMPORTED = 0
        const val NOT_A_CACHE_OF_THIS_TITLE = 1
        const val IO_ERROR = 2
    }

    object CacheKind {
        const val SHADERS = 0
        const val PIPELINES = 1
    }

    /** [result] see [ImportResult], [kind] see [CacheKind]. */
    data class ImportOutcome(val result: Int, val kind: Int, val addedCount: Int, val totalCount: Int)

    @JvmStatic
    private external fun getCacheInfo(titleId: Long): LongArray?

    fun getInfo(titleId: Long): CacheInfo {
        val info = getCacheInfo(titleId)?.takeIf { it.size == 4 } ?: longArrayOf(-1, -1, -1, -1)
        return CacheInfo(info[0], info[1], info[2], info[3])
    }

    /** {shaders file, pipelines file}; they may not exist. */
    @JvmStatic
    external fun getTransferableCachePaths(titleId: Long): Array<String>

    @JvmStatic
    private external fun importTransferableCache(titleId: Long, sourcePath: String, allowLegacy: Boolean): IntArray?

    /**
     * Adds the entries of a transferable cache file that the game's cache doesn't have yet. The file must belong to
     * this game (checked like the core does); [allowLegacy] accepts the old title-independent shader format, only
     * pass it when the file name names this game.
     */
    fun import(titleId: Long, sourcePath: String, allowLegacy: Boolean): ImportOutcome {
        val result = importTransferableCache(titleId, sourcePath, allowLegacy)?.takeIf { it.size == 4 }
            ?: return ImportOutcome(ImportResult.IO_ERROR, 0, 0, 0)
        return ImportOutcome(result[0], result[1], result[2], result[3])
    }
}
