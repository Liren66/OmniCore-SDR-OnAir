package io.github.adswill.onair.ui

import io.github.adswill.onair.usb.UsbCatalog
import io.github.adswill.onair.usb.UsbDeviceRef
import io.github.adswill.onair.usb.UsbRadio
import io.github.adswill.onair.usb.UsbRadioStatus
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.*
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class UsbRadioUiAdapterTest {
    @Test fun omniCoreAppearsBecomesSelectableAndDisappearsOnDetach() = runTest {
        val dev = UsbDeviceRef("/dev/bus/usb/002/002", 0x1a86, 0x5537)
        val radio = UsbRadio(dev, UsbCatalog.identify(dev.vid, dev.pid, null, null)!!, UsbRadioStatus.NEEDS_PERMISSION)
        val radios = MutableStateFlow(listOf(radio))
        val selected = MutableStateFlow<String?>(null)
        val permissions = ArrayList<String>()
        val selections = ArrayList<String>()
        var refreshes = 0
        val adapter = UsbRadioUiAdapter(radios, selected, backgroundScope,
            onRefresh = { refreshes++ }, onPermission = { permissions += it },
            onSelect = { selections += it.key; selected.value = it.key })
        runCurrent()
        assertEquals("OmniCore SDR", adapter.devices.value.single().name)
        assertFalse(adapter.devices.value.single().permitted)
        adapter.select(dev.name)
        assertTrue(selections.isEmpty())
        adapter.refresh()
        adapter.requestPermission(dev.name)
        assertEquals(1, refreshes)
        assertEquals(listOf(dev.name), permissions)

        radios.value = listOf(radio.copy(status = UsbRadioStatus.READY))
        runCurrent()
        assertTrue(adapter.devices.value.single().permitted)
        assertFalse(adapter.devices.value.single().selected)
        adapter.select(dev.name)
        runCurrent()
        assertEquals(listOf(dev.name), selections)
        assertTrue(adapter.devices.value.single().selected)

        radios.value = emptyList()
        runCurrent()
        assertTrue(adapter.devices.value.isEmpty())
        adapter.select(dev.name)
        assertEquals(1, selections.size)
    }
}
