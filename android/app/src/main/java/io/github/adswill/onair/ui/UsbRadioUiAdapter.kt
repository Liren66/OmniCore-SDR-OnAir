package io.github.adswill.onair.ui

import io.github.adswill.onair.usb.UsbRadio
import io.github.adswill.onair.usb.UsbRadioStatus
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn

/** Connect the live USB controller to the source picker, including permission and unplug events. */
class UsbRadioUiAdapter(
    private val radios: StateFlow<List<UsbRadio>>,
    selectedKey: Flow<String?>,
    scope: CoroutineScope,
    private val onRefresh: () -> Unit,
    private val onPermission: (String) -> Unit,
    private val onSelect: (UsbRadio) -> Unit,
) : UsbRadios {
    override val devices: StateFlow<List<UsbRadioInfo>> = combine(radios, selectedKey) { list, key ->
        list.map { r ->
            UsbRadioInfo(r.key, r.label, when (r.status) {
                UsbRadioStatus.READY -> "Ready"
                UsbRadioStatus.ASKING -> "Waiting for USB permission"
                UsbRadioStatus.NEEDS_PERMISSION -> "Tap to allow OnAir to use it"
                else -> r.message
            }, r.ready, r.ready && r.key == key)
        }
    }.stateIn(scope, SharingStarted.Eagerly, emptyList())

    override fun refresh() = onRefresh()
    override fun requestPermission(id: String) = onPermission(id)
    override fun select(id: String) {
        radios.value.firstOrNull { it.key == id && it.ready }?.let(onSelect)
    }
}
