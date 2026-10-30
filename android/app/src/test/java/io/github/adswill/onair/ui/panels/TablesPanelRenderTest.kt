package io.github.adswill.onair.ui.panels

import android.app.Application
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.hasSetTextAction
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performTextReplacement
import io.github.adswill.onair.OnAirTheme
import io.github.adswill.onair.R
import io.github.adswill.onair.model.ResultTable
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], application = Application::class)
class TablesPanelRenderTest {
    @get:Rule val compose = createComposeRule()

    @Test fun emptyTablesCanReceiveDataAndBecomeEmptyAgain() {
        val tables = mutableStateOf(emptyList<ResultTable>())
        compose.setContent { OnAirTheme { TablesContent(tables.value) } }
        val empty = RuntimeEnvironment.getApplication().getString(R.string.panel_nothing)
        compose.onNodeWithText(empty).assertExists()
        compose.runOnIdle { tables.value = listOf(ResultTable("Packets", listOf("Callsign"), listOf(listOf("TEST123")))) }
        compose.onNodeWithText("TEST123").assertExists()
        compose.runOnIdle { tables.value = emptyList() }
        compose.onNodeWithText(empty).assertExists()
    }

    @Test fun emptyRowsAndNoSearchMatchesDoNotBreakComposition() {
        val tables = mutableStateOf(listOf(ResultTable("Packets", listOf("Callsign"), emptyList())))
        compose.setContent { OnAirTheme { TablesContent(tables.value) } }
        val noMatch = RuntimeEnvironment.getApplication().getString(R.string.panel_no_match)
        compose.onNodeWithText(noMatch).assertExists()
        compose.runOnIdle { tables.value = listOf(ResultTable("Packets", listOf("Callsign"), listOf(listOf("TEST123")))) }
        compose.onNodeWithText("TEST123").assertExists()
        compose.onNode(hasSetTextAction()).performTextReplacement("no-such-callsign")
        compose.onNodeWithText(noMatch).assertExists()
        compose.onNode(hasSetTextAction()).performTextReplacement("")
        compose.onNodeWithText("TEST123").assertExists()
    }
}
