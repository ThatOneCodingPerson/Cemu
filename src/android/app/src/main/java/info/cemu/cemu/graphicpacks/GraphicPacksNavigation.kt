package info.cemu.cemu.graphicpacks

import androidx.navigation.NavGraphBuilder
import androidx.navigation.NavHostController
import androidx.navigation.compose.composable
import androidx.navigation.toRoute
import info.cemu.cemu.common.ui.extensions.navigateBackSafely
import kotlinx.serialization.Serializable

@Serializable
object GraphicPacksRoute

/** Only the graphic packs of one title (game list > long-press > "Graphic packs…"). */
@Serializable
data class GraphicPacksForTitleRoute(val titleId: Long, val titleName: String)

fun NavGraphBuilder.graphicPacksNavigation(navController: NavHostController) {
    composable<GraphicPacksRoute> {
        GraphicPacksScreen(
            navigateBack = { navController.navigateBackSafely() }
        )
    }
    composable<GraphicPacksForTitleRoute> { navBackStackEntry ->
        val route = navBackStackEntry.toRoute<GraphicPacksForTitleRoute>()
        GraphicPacksScreen(
            navigateBack = { navController.navigateBackSafely() },
            titleFilter = TitleFilter(route.titleId, route.titleName),
        )
    }
}
