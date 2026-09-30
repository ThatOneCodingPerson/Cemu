package info.cemu.cemu.settings.customdrivers

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.animateContentSize
import androidx.compose.foundation.basicMarquee
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.rotate
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import info.cemu.cemu.R
import info.cemu.cemu.common.customdrivers.DriverMetadata
import info.cemu.cemu.common.ui.components.ScreenContentLazy
import info.cemu.cemu.common.ui.localization.tr
import kotlinx.coroutines.launch

@Composable
fun CustomDriversScreen(
    navigateBack: () -> Unit,
    goToDriverDownload: () -> Unit,
    customDriversViewModel: CustomDriversViewModel = viewModel(),
) {
    val coroutineScope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }
    val installedDrivers by customDriversViewModel.installedDrivers.collectAsState()
    val isSystemDriverSelected by customDriversViewModel.isSystemDriverSelected.collectAsState()
    val isDriverInstallInProgress by customDriversViewModel.isDriverInstallInProgress.collectAsState()
    val context = LocalContext.current

    LaunchedEffect(Unit) {
        customDriversViewModel.refresh()
    }

    val customDriversInstallLauncher =
        rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri == null) return@rememberLauncherForActivityResult

            customDriversViewModel.installDriver(context, uri) { installStatus ->
                val message = driverInstallStatusMessage(installStatus)

                coroutineScope.launch {
                    snackbarHostState.currentSnackbarData?.dismiss()
                    snackbarHostState.showSnackbar(message)
                }
            }
        }

    ScreenContentLazy(
        snackbarHost = { SnackbarHost(hostState = snackbarHostState) },
        appBarText = tr("Custom drivers"),
        navigateBack = navigateBack,
        actions = {
            IconButton(onClick = goToDriverDownload) {
                Icon(
                    painter = painterResource(R.drawable.ic_download),
                    contentDescription = tr("Download drivers")
                )
            }
            IconButton(onClick = { customDriversInstallLauncher.launch(arrayOf("application/zip")) }) {
                Icon(
                    painter = painterResource(R.drawable.ic_add),
                    contentDescription = null
                )
            }
        },
    ) {
        item {
            DefaultDriverNote()
        }
        item {
            SystemDriverListItem(
                selected = isSystemDriverSelected,
                onSelect = customDriversViewModel::setSystemDriverSelected
            )
        }
        items(installedDrivers) {
            CustomDriverListItem(
                driver = it,
                onDelete = { customDriversViewModel.deleteDriver(it) },
                onSelect = {
                    if (it.selected) return@CustomDriverListItem
                    customDriversViewModel.setDriverSelected(it)
                    // the choice here applies to every game; point to the per-game setting
                    coroutineScope.launch {
                        snackbarHostState.currentSnackbarData?.dismiss()
                        snackbarHostState.showSnackbar(
                            message = tr("This driver is now used for every game. To use it for one game only, choose it in that game's profile instead."),
                            duration = SnackbarDuration.Long,
                        )
                    }
                }
            )
        }
    }

    if (isDriverInstallInProgress)
        DriverInstallProgressDialog()
}

@Composable
private fun DriverInstallProgressDialog() {
    AlertDialog(
        title = {
            Text(tr("Installing"))
        },
        text = {
            Column(
                modifier = Modifier.padding(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                Text(tr("Installing driver in progress"))
                LinearProgressIndicator(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(vertical = 8.dp)
                )
            }
        },
        onDismissRequest = {},
        confirmButton = {},
        dismissButton = {}
    )
}

/** The system driver runs almost every game; a different driver is best chosen per game. */
@Composable
fun DefaultDriverNote(modifier: Modifier = Modifier) {
    Card(
        modifier = modifier
            .fillMaxWidth()
            .padding(8.dp),
        colors = CardDefaults.cardColors(
            containerColor = MaterialTheme.colorScheme.secondaryContainer,
            contentColor = MaterialTheme.colorScheme.onSecondaryContainer,
        ),
    ) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(
                text = tr("The system driver works for almost every Wii U game"),
                fontSize = 16.sp,
                fontWeight = FontWeight.Bold,
            )
            Text(
                text = tr("Keep it as the default here. If one game has problems, give only that game another driver: long-press the game, then Edit game profile > Custom driver. Your other games keep the driver that works for them."),
                fontSize = 14.sp,
            )
        }
    }
}

@Composable
private fun SystemDriverListItem(selected: Boolean, onSelect: () -> Unit) {
    DriverListItem(
        driverLabel = tr("System driver (default, recommended)"),
        selected = selected,
        onSelect = onSelect
    )
}

@Composable
private fun CustomDriverListItem(driver: Driver, onDelete: () -> Unit, onSelect: () -> Unit) {
    var showDriverInfo by remember { mutableStateOf(false) }

    DriverListItem(
        driverLabel = driver.metadata.name,
        selected = driver.selected,
        onSelect = onSelect,
        labelExtraContent = {
            IconButton(onClick = { showDriverInfo = !showDriverInfo }) {
                Icon(
                    modifier = Modifier.rotate(if (showDriverInfo) 180f else 0f),
                    painter = painterResource(R.drawable.ic_arrow_drop_down),
                    contentDescription = null
                )
            }
            IconButton(onClick = onDelete) {
                Icon(
                    painter = painterResource(R.drawable.ic_delete),
                    contentDescription = null
                )
            }
        }
    ) {
        if (showDriverInfo) {
            DriverMetadataInfo(driver.metadata)
        }
    }
}

@Composable
private fun DriverListItem(
    driverLabel: String,
    selected: Boolean,
    onSelect: () -> Unit,
    labelExtraContent: @Composable RowScope.() -> Unit = {},
    content: @Composable ColumnScope.() -> Unit = {},
) {
    Card(
        modifier = Modifier
            .animateContentSize()
            .fillMaxWidth()
            .padding(8.dp),
        onClick = onSelect
    ) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            RadioButton(selected = selected, onClick = onSelect)
            Text(
                modifier = Modifier
                    .padding(horizontal = 4.dp)
                    .basicMarquee(iterations = Int.MAX_VALUE)
                    .weight(1.0f),
                text = driverLabel,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
                fontSize = 18.sp,
                fontWeight = FontWeight.Bold,
            )
            labelExtraContent()
        }
        content()
    }
}

@Composable
private fun DriverMetadataInfo(metadata: DriverMetadata) {
    Column(
        modifier = Modifier
            .fillMaxWidth()
            .padding(8.dp)
    ) {
        DriverMetadataInfo(tr("Description"), metadata.description)
        DriverMetadataInfo(tr("Author"), metadata.author)
        DriverMetadataInfo(tr("Package version"), metadata.packageVersion)
        DriverMetadataInfo(tr("Vendor"), metadata.vendor)
        DriverMetadataInfo(tr("Driver version"), metadata.driverVersion)
        DriverMetadataInfo(tr("Min api"), metadata.minApi)
    }
}

@Composable
private fun <T> DriverMetadataInfo(label: String, info: T) {
    Text(
        modifier = Modifier.padding(start = 8.dp, end = 8.dp, top = 2.dp),
        fontSize = 16.sp,
        fontWeight = FontWeight.Medium,
        text = label
    )
    Text(
        modifier = Modifier.padding(start = 8.dp, end = 8.dp, bottom = 2.dp),
        fontSize = 14.sp,
        text = info.toString(),
    )
}
