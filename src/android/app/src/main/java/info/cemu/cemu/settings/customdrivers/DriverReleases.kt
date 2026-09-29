package info.cemu.cemu.settings.customdrivers

import info.cemu.cemu.common.ui.localization.trNoop
import io.ktor.client.HttpClient
import io.ktor.client.request.get
import io.ktor.client.request.header
import io.ktor.client.statement.bodyAsText
import io.ktor.http.HttpStatusCode
import io.ktor.http.isSuccess
import kotlinx.coroutines.CancellationException
import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import java.util.concurrent.ConcurrentHashMap

/**
 * GitHub repos publishing adrenotools driver zips (meta.json + library at the zip root). The list follows the repos
 * other Android emulators offer (see docs/claude/FEATURE_RESEARCH.md §1). Checked 2026-09-28.
 */
/** [description] is untranslated, pass it to tr(). */
enum class DriverRepository(val displayName: String, val repo: String, val description: String) {
    MrPurpleTurnip(
        "Mr. Purple Turnip",
        "MrPurple666/purple-turnip",
        trNoop("Turnip (open source) for Adreno 6xx and 7xx"),
    ),
    KimchiTurnip(
        "KIMCHI Turnip",
        "K11MCH1/AdrenoToolsDrivers",
        trNoop("Turnip and repackaged Qualcomm drivers"),
    ),
    GameHubAdreno8xx(
        "GameHub Adreno 8xx",
        "crueter/GameHub-8Elite-Drivers",
        trNoop("Qualcomm drivers for Adreno 8xx"),
    ),
    WeabChanTurnip(
        "Weab-Chan Turnip",
        "Weab-chan/freedreno_turnip-CI",
        trNoop("Turnip CI builds"),
    ),
    WhitebelyashTurnip(
        "Whitebelyash Turnip",
        "whitebelyash/AdrenoToolsDrivers", // formerly whitebelyash/freedreno_turnip-CI
        trNoop("Mainline and Adreno 8xx Turnip builds"),
    ),
}

@Serializable
data class GitHubRelease(
    @SerialName("tag_name")
    val tagName: String,
    val name: String? = null,
    @SerialName("published_at")
    val publishedAt: String? = null,
    val prerelease: Boolean = false,
    val draft: Boolean = false,
    val assets: List<GitHubReleaseAsset> = emptyList(),
) {
    val title get() = name?.takeIf { it.isNotBlank() } ?: tagName
    val zipAssets get() = assets.filter { it.name.endsWith(".zip", ignoreCase = true) }
}

@Serializable
data class GitHubReleaseAsset(
    val name: String,
    val size: Long = 0,
    @SerialName("browser_download_url")
    val downloadUrl: String,
)

sealed interface DriverReleasesResult {
    data class Success(val releases: List<GitHubRelease>) : DriverReleasesResult

    /** GitHub allows 60 unauthenticated API requests per hour and IP. [resetEpochSeconds] is when it resets. */
    data class RateLimited(val resetEpochSeconds: Long?) : DriverReleasesResult

    data object Error : DriverReleasesResult
}

private val json = Json { ignoreUnknownKeys = true }

private const val CACHE_DURATION_MILLIS = 15 * 60 * 1000L

private class CachedReleases(val timeMillis: Long, val releases: List<GitHubRelease>)

// per process, so reopening the screen doesn't spend the small API budget again
private val releasesCache = ConcurrentHashMap<DriverRepository, CachedReleases>()

/** Parses a GitHub "list releases" response. Newest first, only releases with a zip to download. */
fun parseDriverReleases(responseBody: String): List<GitHubRelease> =
    json.decodeFromString<List<GitHubRelease>>(responseBody)
        .filter { !it.draft && it.zipAssets.isNotEmpty() }
        // the API order isn't always by date; ISO 8601 UTC timestamps sort as strings
        .sortedByDescending { it.publishedAt ?: "" }

suspend fun fetchDriverReleases(client: HttpClient, repository: DriverRepository): DriverReleasesResult {
    releasesCache[repository]?.let {
        if (System.currentTimeMillis() - it.timeMillis < CACHE_DURATION_MILLIS)
            return DriverReleasesResult.Success(it.releases)
    }

    val response = try {
        client.get("https://api.github.com/repos/${repository.repo}/releases?per_page=50") {
            header("Accept", "application/vnd.github+json")
            header("X-GitHub-Api-Version", "2022-11-28")
        }
    } catch (exception: CancellationException) {
        throw exception
    } catch (_: Exception) {
        return DriverReleasesResult.Error
    }

    val isRateLimited = response.status == HttpStatusCode.TooManyRequests ||
            (response.status == HttpStatusCode.Forbidden && response.headers["x-ratelimit-remaining"] == "0")
    if (isRateLimited) {
        return DriverReleasesResult.RateLimited(response.headers["x-ratelimit-reset"]?.toLongOrNull())
    }
    if (!response.status.isSuccess()) {
        return DriverReleasesResult.Error
    }

    val releases = try {
        parseDriverReleases(response.bodyAsText())
    } catch (exception: CancellationException) {
        throw exception
    } catch (_: Exception) {
        return DriverReleasesResult.Error
    }

    releasesCache[repository] = CachedReleases(System.currentTimeMillis(), releases)
    return DriverReleasesResult.Success(releases)
}

/**
 * A suggested starting point for an Adreno GPU, following the picks other Android emulators ship (Eden's driver
 * fetcher, 2026-09). [releaseTagPart] selects a specific release by a part of its tag, null means the newest one.
 */
data class DriverSuggestion(val repository: DriverRepository, val releaseTagPart: String?) {
    fun matches(repository: DriverRepository, release: GitHubRelease, newestRelease: GitHubRelease?): Boolean {
        if (repository != this.repository)
            return false
        return if (releaseTagPart != null) release.tagName.contains(releaseTagPart) else release == newestRelease
    }
}

private val adrenoModelRegex = Regex("""Adreno \(TM\) (\d{3})""")

/** The Adreno model number from a Vulkan device name like "Adreno (TM) 740", null for other GPUs. */
fun parseAdrenoModel(deviceName: String): Int? =
    adrenoModelRegex.find(deviceName)?.groupValues?.get(1)?.toIntOrNull()

fun suggestDriver(adrenoModel: Int?): DriverSuggestion? = when (adrenoModel) {
    null -> null
    in 600..639 -> DriverSuggestion(DriverRepository.MrPurpleTurnip, "EOL-24.3.4")
    in 640..699 -> DriverSuggestion(DriverRepository.MrPurpleTurnip, "T19")
    in 700..710 -> DriverSuggestion(DriverRepository.KimchiTurnip, "v25.2.0-rc.05")
    in 711..799 -> DriverSuggestion(DriverRepository.MrPurpleTurnip, "T23")
    in 800..899 -> DriverSuggestion(DriverRepository.GameHubAdreno8xx, null)
    else -> null
}
