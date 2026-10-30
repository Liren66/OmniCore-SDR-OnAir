package io.github.adswill.onair

import android.app.Application
import io.github.adswill.onair.diag.AppLog
import io.github.adswill.onair.diag.CrashLog
import io.github.adswill.onair.model.SourceKind
import io.github.adswill.onair.ui.UiGraph
import io.github.adswill.onair.ui.UiHooks
import io.github.adswill.onair.ui.UsbRadioUiAdapter
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.launch

class OnAirApplication : Application() {
    override fun onCreate() {
        super.onCreate()
        CrashLog.install(this)                  // first: a crash anywhere after this line is recorded
        AppGraph.init(this)
        AppLog.i("app", CrashLog.header())
        val repo = AppGraph.repository            // starts the receiver thread, reads the settings and the mode table
        UiHooks.usb = UsbRadioUiAdapter(
            repo.radios.usbRadios,
            combine(repo.settings, repo.radios.usbRadios) { settings, _ ->
                if (settings.source == SourceKind.USB) repo.radios.usbFor(settings)?.key else null
            },
            UiGraph.scope,
            onRefresh = { UiGraph.scope.launch { repo.radios.usb.rescan(askPermission = true) }; Unit },
            onPermission = repo.radios::requestUsbPermission,
            onSelect = { radio -> repo.radios.selectUsb(radio); repo.chooseSource(SourceKind.USB) },
        )
    }
}
