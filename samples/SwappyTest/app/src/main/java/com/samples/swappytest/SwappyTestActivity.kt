/*
 * Copyright 2026 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.samples.swappytest

import android.app.Activity
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.Menu
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.View
import android.view.WindowInsets
import android.view.WindowInsetsController
import android.view.WindowManager
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.CheckBox
import android.widget.LinearLayout
import android.widget.Spinner
import android.widget.TextView
import android.window.OnBackInvokedCallback
import android.window.OnBackInvokedDispatcher
import java.util.Locale

class SwappyTestActivity : Activity(), SurfaceHolder.Callback {

    class ScenarioItem(val id: String, val displayName: String) {
        override fun toString(): String = displayName
    }

    class RenderApiItem(val api: Int, val shortLabel: String, val displayName: String) {
        override fun toString(): String = displayName
    }

    class PacingStrategyItem(val strategy: Int, val shortLabel: String, val displayName: String) {
        override fun toString(): String = displayName
    }

    class PipelineModeItem(val mode: String, val shortLabel: String, val displayName: String) {
        override fun toString(): String = displayName
    }

    // What the native benchmark reports to the activity, filled by nGetUiSnapshot().
    class UiSnapshot {
        @JvmField var testPhase: Int = TEST_PHASE_READY
        @JvmField var runTainted: Boolean = false
        @JvmField var runFailed: Boolean = false
        @JvmField var taintReason: String = ""
        @JvmField var testScenario: String = ""
        // The CalibrationStatus of each CalibrationKind's cache.
        @JvmField var calibrationStatus: IntArray = IntArray(CALIBRATION_KIND_COUNT)
        // The calibrations a failed scenario run found missing or invalid.
        @JvmField var missingCalibrations: Int = 0
    }

    // One calibration run: the calibrations it runs on render API api.
    private data class CalibrationRun(val api: Int, val kinds: Int)

    private data class TestReportSnapshot(
        val scenario: String,
        val scenarioDesc: String,
        val renderApiLabel: String,
        val pacingLabel: String,
        val pipelineLabel: String,
        val status: Int,
        val taintReason: String,
        val diagnosticsSummary: String
    )

    private var mSurfaceView: SurfaceView? = null
    private var mSettingsLayout: LinearLayout? = null
    private var mSpinnerTestSelection: Spinner? = null
    private var mTextScenarioDescription: TextView? = null
    private var mSpinnerRenderApi: Spinner? = null
    private var mSpinnerPacingStrategy: Spinner? = null
    private var mSpinnerPipelineMode: Spinner? = null
    private var mCheckboxShowOverlay: CheckBox? = null
    private var mButtonCalibrateCpu: Button? = null
    private var mButtonCalibrateGpu: Button? = null
    private var mButtonCalibrateAll: Button? = null
    private var mTextCalibrationStatus: TextView? = null
    private var mButtonStartTest: Button? = null
    private var mButtonViewResults: Button? = null

    private var mResultsLayout: LinearLayout? = null
    private var mResultsButtonBackToSettings: Button? = null
    private var mResultsButtonStartTest: Button? = null
    private var mResultsTitleText: TextView? = null
    private var mResultsVerdictBadge: TextView? = null
    private var mResultsScenarioDesc: TextView? = null
    private var mResultsSummaryText: TextView? = null
    private var mResultsDiagnosticsTable: TextView? = null

    private var mScenarios: Array<ScenarioItem> = emptyArray()
    private var mActiveScenarioId: String = "S1"
    private var mCustomScenarioActive: Boolean = false
    private var mCustomScenarioSpinnerPos: Int = 0
    private var mCustomScenarioDescription: String = "CUSTOM"
    private var mActiveRenderApi: Int = RENDER_API_GLES
    private var mActivePacingStrategy: Int = PACING_CLASSIC
    private var mActivePipelineMode: String = "auto"
    private var mLastReportSnapshot: TestReportSnapshot? = null

    private var mTestActive: Boolean = false
    // The calibrations the active run runs; 0 when it runs a scenario.
    private var mCalibrationKinds: Int = 0
    // The calibration runs still to come after the active one, which switch
    // the render API, the calibrations they all run, and the failure reasons
    // of those already run.
    private val mPendingCalibrationRuns = ArrayDeque<CalibrationRun>()
    private var mRequestedCalibrations: Int = 0
    private val mCalibrationFailures = ArrayList<String>()
    private var mIsRendering: Boolean = false
    private var mStartedManually: Boolean = true
    private var mAutoStart: Boolean = false
    private var mIsFinishingAfterAutomatedRun: Boolean = false

    private var mBackInvokedCallbackObj: Any? = null
    private var mBackInvokedCallbackRegistered: Boolean = false

    private val mUiSnapshot = UiSnapshot()
    private val mMainHandler = Handler(Looper.getMainLooper())
    private val mFinishAfterAutomatedRun = Runnable {
        if (!isFinishing) {
            finish()
        }
    }
    private val mPhasePollRunnable = object : Runnable {
        override fun run() {
            if (!mTestActive || !mIsRendering) {
                return
            }
            nGetUiSnapshot(mUiSnapshot)
            if (mUiSnapshot.testPhase == TEST_PHASE_COMPLETE) {
                onTestComplete()
                return
            }
            mMainHandler.postDelayed(this, PHASE_POLL_INTERVAL_MS)
        }
    }

    external fun nInit()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O_MR1) {
            setShowWhenLocked(true)
            setTurnScreenOn(true)
        } else {
            @Suppress("DEPRECATION")
            window.addFlags(
                WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED
                    or WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON
                    or WindowManager.LayoutParams.FLAG_DISMISS_KEYGUARD
            )
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            window.attributes.layoutInDisplayCutoutMode =
                WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        setContentView(R.layout.activity_swappy_test)
        hideSystemUI()

        mSurfaceView = findViewById(R.id.surface_view)
        mSurfaceView?.holder?.addCallback(this)
        mSurfaceView?.setOnTouchListener { _, event ->
            val action = event.actionMasked
            val x = event.x
            val y = event.y
            val eventTimeNs = event.eventTime * 1_000_000L
            nOnTouchEvent(action, x, y, eventTimeNs, mStartedManually && mTestActive)
            true
        }

        mSettingsLayout = findViewById(R.id.settings_layout)
        mSpinnerTestSelection = findViewById(R.id.spinnerTestSelection)
        mTextScenarioDescription = findViewById(R.id.text_scenario_description)
        mSpinnerRenderApi = findViewById(R.id.spinnerRenderApi)
        mSpinnerPacingStrategy = findViewById(R.id.spinnerPacingStrategy)
        mSpinnerPipelineMode = findViewById(R.id.spinnerPipelineMode)
        mCheckboxShowOverlay = findViewById(R.id.checkboxShowOverlay)
        mCheckboxShowOverlay?.setOnCheckedChangeListener { _, isChecked ->
            nSetOverlayEnabled(isChecked)
        }
        mButtonCalibrateCpu = findViewById(R.id.buttonCalibrateCpu)
        mButtonCalibrateGpu = findViewById(R.id.buttonCalibrateGpu)
        mButtonCalibrateAll = findViewById(R.id.buttonCalibrateAll)
        mTextCalibrationStatus = findViewById(R.id.text_calibration_status)
        mButtonStartTest = findViewById(R.id.buttonStartTest)
        mButtonViewResults = findViewById(R.id.buttonViewResults)

        mResultsLayout = findViewById(R.id.results_layout)
        mResultsButtonBackToSettings = findViewById(R.id.results_buttonBackToSettings)
        mResultsButtonStartTest = findViewById(R.id.results_buttonStartTest)
        mResultsTitleText = findViewById(R.id.results_title_text)
        mResultsVerdictBadge = findViewById(R.id.results_verdict_badge)
        mResultsScenarioDesc = findViewById(R.id.results_scenario_desc)
        mResultsSummaryText = findViewById(R.id.results_summary_text)
        mResultsDiagnosticsTable = findViewById(R.id.results_diagnostics_table)

        mButtonCalibrateCpu?.setOnClickListener {
            startFromSettings(calibrationBit(CALIBRATION_CPU))
        }
        mButtonCalibrateGpu?.setOnClickListener {
            startFromSettings(calibrationBit(gpuCalibrationKind(selectedRenderApi())))
        }
        mButtonCalibrateAll?.setOnClickListener { startFromSettings(ALL_CALIBRATIONS) }
        mButtonStartTest?.setOnClickListener { startFromSettings(0) }
        mResultsButtonStartTest?.setOnClickListener { startFromSettings(0) }
        mButtonViewResults?.setOnClickListener { showLastTestResultsScreen() }
        mResultsButtonBackToSettings?.setOnClickListener { showSettingsScreen() }

        nInit()
        filesDir?.let {
            nSetOutputDirectory(it.absolutePath)
        }

        setupSettingsSpinners()

        applyIntentAndRouteScreen(intent)
    }

    private fun setupSettingsSpinners() {
        val count = nGetScenarioCount()
        val scenarioList = ArrayList<ScenarioItem>(count)
        for (i in 0 until count) {
            val id = nGetScenarioId(i)
            val desc = nGetScenarioDescription(i)
            scenarioList.add(ScenarioItem(id ?: "", desc ?: (id ?: "")))
        }
        mScenarios = scenarioList.toTypedArray()

        val scenarioAdapter =
            ArrayAdapter(this, R.layout.spinner_item_scenario, mScenarios).apply {
                setDropDownViewResource(R.layout.spinner_dropdown_item_scenario)
            }
        mSpinnerTestSelection?.adapter = scenarioAdapter
        mSpinnerTestSelection?.onItemSelectedListener =
            object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(
                    parent: AdapterView<*>?,
                    view: View?,
                    position: Int,
                    id: Long
                ) {
                    if (mCustomScenarioActive && position != mCustomScenarioSpinnerPos) {
                        mCustomScenarioActive = false
                    }
                    if (!mCustomScenarioActive) {
                        updateScenarioDescription(position)
                    }
                }

                override fun onNothingSelected(parent: AdapterView<*>?) {}
            }
        updateScenarioDescription(0)

        val apiAdapter =
            ArrayAdapter(this, R.layout.spinner_item_scenario, RENDER_APIS).apply {
                setDropDownViewResource(R.layout.spinner_dropdown_item_scenario)
            }
        mSpinnerRenderApi?.adapter = apiAdapter
        mSpinnerRenderApi?.onItemSelectedListener =
            object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(
                    parent: AdapterView<*>?,
                    view: View?,
                    position: Int,
                    id: Long
                ) {
                    mButtonCalibrateGpu?.text = "Calibrate GPU (" +
                        CALIBRATION_LABELS[gpuCalibrationKind(selectedRenderApi())] + ")"
                }

                override fun onNothingSelected(parent: AdapterView<*>?) {}
            }

        val pacingAdapter =
            ArrayAdapter(this, R.layout.spinner_item_scenario, PACING_STRATEGIES).apply {
                setDropDownViewResource(R.layout.spinner_dropdown_item_scenario)
            }
        mSpinnerPacingStrategy?.adapter = pacingAdapter

        val pipelineAdapter =
            ArrayAdapter(this, R.layout.spinner_item_scenario, PIPELINE_MODES).apply {
                setDropDownViewResource(R.layout.spinner_dropdown_item_scenario)
            }
        mSpinnerPipelineMode?.adapter = pipelineAdapter
    }

    private fun selectScenarioById(scenarioId: String?) {
        if (scenarioId == null) return
        for (i in mScenarios.indices) {
            if (mScenarios[i].id.equals(scenarioId, ignoreCase = true)) {
                mSpinnerTestSelection?.setSelection(i)
                mActiveScenarioId = mScenarios[i].id
                updateScenarioDescription(i)
                break
            }
        }
    }

    private fun updateScenarioDescription(position: Int) {
        val textDesc = mTextScenarioDescription ?: return
        var desc: String? = null
        try {
            desc = nGetScenarioDescription(position)
        } catch (e: UnsatisfiedLinkError) {
            Log.w(APP_NAME, "nGetScenarioDescription failed: ${e.message}")
        }
        if (desc.isNullOrEmpty()) {
            desc = if (position in mScenarios.indices) {
                mScenarios[position].displayName
            } else {
                "Selected scenario"
            }
        }
        textDesc.text = desc
    }

    private fun selectRenderApi(api: Int) {
        for (i in RENDER_APIS.indices) {
            if (RENDER_APIS[i].api == api) {
                mSpinnerRenderApi?.setSelection(i)
                break
            }
        }
    }

    private fun selectPacingStrategy(strategy: Int) {
        mActivePacingStrategy = strategy
        for (i in PACING_STRATEGIES.indices) {
            if (PACING_STRATEGIES[i].strategy == strategy) {
                mSpinnerPacingStrategy?.setSelection(i)
                break
            }
        }
    }

    private fun selectPipelineMode(mode: String?) {
        val resolvedMode = mode ?: "auto"
        mActivePipelineMode = resolvedMode
        for (i in PIPELINE_MODES.indices) {
            if (PIPELINE_MODES[i].mode.equals(resolvedMode, ignoreCase = true)) {
                mSpinnerPipelineMode?.setSelection(i)
                break
            }
        }
    }

    private fun applyIntentAndRouteScreen(intent: Intent?) {
        stopNativeRender()
        mMainHandler.removeCallbacks(mFinishAfterAutomatedRun)
        mIsFinishingAfterAutomatedRun = false

        val customScenarioSpec = extractCustomScenarioSpec(intent)

        val hasScenarioExtra =
            intent != null &&
                (intent.hasExtra("scenario") ||
                    intent.hasExtra("SCENARIO") ||
                    customScenarioSpec != null)
        val autoExitExtra = intent != null && getBooleanOption(intent, "auto_exit", false)
        val requestedApi = extractRenderApi(intent)
        val calibrationKinds =
            extractCalibrationKinds(intent, if (requestedApi >= 0) requestedApi else nGetRenderApi())
        val calibrateExtra = calibrationKinds != 0

        mStartedManually = !autoExitExtra && !hasScenarioExtra && !calibrateExtra
        mAutoStart = hasScenarioExtra || autoExitExtra || calibrateExtra

        var selectedScenario = "S1"
        var swappy = true
        var set30Fps = false

        if (intent != null) {
            applyGlobalSwappyIntentExtras(intent)
            swappy = getBooleanOption(intent, "swappy", true)
            set30Fps = getBooleanOption(intent, "set_30_fps_limit", false)
            var scenario = getStringOption(intent, "scenario", null)
            if (scenario == null) {
                scenario = getStringOption(intent, "SCENARIO", null)
            }
            if (scenario.isNullOrEmpty() && customScenarioSpec != null) {
                scenario = "CUSTOM"
            }
            if (!scenario.isNullOrEmpty()) {
                selectedScenario = scenario
            }
            if ("S5".equals(selectedScenario, ignoreCase = true) ||
                "H7".equals(selectedScenario, ignoreCase = true)
            ) {
                set30Fps = true
            }
            val runIdx = intent.getIntExtra("run_idx", intent.getIntExtra("run_iteration", 1))
            nSetRunIteration(runIdx)
        }

        val isCustomScenario =
            customScenarioSpec != null || "CUSTOM".equals(selectedScenario, ignoreCase = true)
        mCustomScenarioActive = isCustomScenario
        mActiveScenarioId = if (isCustomScenario) "CUSTOM" else selectedScenario
        mActiveRenderApi = nGetRenderApi()
        if (isCustomScenario) {
            val spec = customScenarioSpec ?: ""
            nSetCustomScenario(spec)
            val compactSpec = spec.trim().replace('\n', ';')
            mCustomScenarioDescription =
                if (compactSpec.isEmpty()) "CUSTOM" else "CUSTOM ($compactSpec)"
            mCustomScenarioSpinnerPos = mSpinnerTestSelection?.selectedItemPosition ?: 0
            mTextScenarioDescription?.text = mCustomScenarioDescription
        } else {
            nSetTestScenario(selectedScenario)
            selectScenarioById(selectedScenario)
        }
        nSetOptions(swappy, set30Fps)
        queueCalibrations(calibrationKinds, mActiveRenderApi)

        selectRenderApi(nGetRenderApi())
        selectPacingStrategy(mActivePacingStrategy)
        selectPipelineMode(mActivePipelineMode)

        val showOverlay =
            getBooleanOption(
                intent,
                "overlay",
                getBooleanOption(intent, "show_overlay", !autoExitExtra)
            )
        mCheckboxShowOverlay?.isChecked = showOverlay
        nSetOverlayEnabled(showOverlay)

        updateLastResultsButton()

        if (mAutoStart) {
            beginTestRun()
        } else {
            showSettingsScreen()
        }
    }

    // The calibrations the calibrate extra, or a legacy alias, requests: a
    // comma-separated list of "cpu", "gles", "vulkan", "gpu" (activeApi's)
    // and "all". true, or no value, calibrates the CPU and activeApi's GPU as
    // the combined calibration did.
    private fun extractCalibrationKinds(intent: Intent?, activeApi: Int): Int {
        if (intent == null) return 0
        val key = arrayOf("calibrate", "recalibrate", "calibration", "force_calibration")
            .firstOrNull { intent.hasExtra(it) } ?: return 0
        val text = (rawExtra(intent, key)?.toString() ?: "").trim().lowercase(Locale.US)
        val cpu = calibrationBit(CALIBRATION_CPU)
        val activeGpu = calibrationBit(gpuCalibrationKind(activeApi))
        if (text.isEmpty()) return cpu or activeGpu
        var kinds = 0
        for (token in text.split(',', ';', ' ').map { it.trim() }.filter { it.isNotEmpty() }) {
            kinds = kinds or when (token) {
                "cpu" -> cpu
                "gles", "gl", "opengl" -> calibrationBit(CALIBRATION_GLES)
                "vulkan", "vk" -> calibrationBit(CALIBRATION_VULKAN)
                "gpu" -> activeGpu
                "all" -> ALL_CALIBRATIONS
                "true", "1", "on" -> cpu or activeGpu
                "false", "0", "off" -> 0
                else -> {
                    Log.w(APP_NAME, "Ignoring unknown calibration kind '$token'")
                    0
                }
            }
        }
        return kinds
    }

    // Queues the runs that calibrate kinds, one per render API whose GPU
    // calibration kinds has, activeApi first, with the CPU calibration in the
    // first run, and selects the first for the next run. No kinds selects
    // the selected scenario. A scenario fails unless the calibrations it
    // needs are cached.
    private fun queueCalibrations(kinds: Int, activeApi: Int) {
        mPendingCalibrationRuns.clear()
        mCalibrationFailures.clear()
        mRequestedCalibrations = kinds
        val otherApi = if (activeApi == RENDER_API_VULKAN) RENDER_API_GLES else RENDER_API_VULKAN
        val cpu = kinds and calibrationBit(CALIBRATION_CPU)
        val gpuApis = listOf(activeApi, otherApi)
            .filter { (kinds and calibrationBit(gpuCalibrationKind(it))) != 0 }
        if (gpuApis.isEmpty()) {
            if (cpu != 0) mPendingCalibrationRuns.add(CalibrationRun(activeApi, cpu))
        } else {
            gpuApis.forEachIndexed { i, api ->
                val gpu = calibrationBit(gpuCalibrationKind(api))
                mPendingCalibrationRuns.add(CalibrationRun(api, if (i == 0) gpu or cpu else gpu))
            }
        }
        selectNextCalibrationRun()
    }

    // Selects the next queued calibration run, switching to its render API,
    // or the selected scenario once none is left.
    private fun selectNextCalibrationRun() {
        val run = mPendingCalibrationRuns.removeFirstOrNull()
        if (run != null) {
            nSetRenderApi(run.api)
            selectRenderApi(run.api)
        }
        mCalibrationKinds = run?.kinds ?: 0
        nSetCalibrationRun(mCalibrationKinds)
    }

    private fun selectedRenderApi(): Int =
        (mSpinnerRenderApi?.selectedItem as? RenderApiItem)?.api ?: RENDER_API_GLES

    private fun startFromSettings(calibrationKinds: Int) {
        mStartedManually = true
        mAutoStart = false

        val scenarioIndex =
            (mSpinnerTestSelection?.selectedItemPosition ?: 0)
                .coerceIn(0, maxOf(0, mScenarios.size - 1))
        val scenarioId =
            if (mCustomScenarioActive) {
                "CUSTOM"
            } else if (scenarioIndex in mScenarios.indices) {
                mScenarios[scenarioIndex].id
            } else {
                "S1"
            }
        mActiveScenarioId = scenarioId

        val renderApi = selectedRenderApi()
        mActiveRenderApi = renderApi

        val pacingItem = mSpinnerPacingStrategy?.selectedItem as? PacingStrategyItem
        val pacingStrategy = pacingItem?.strategy ?: PACING_CLASSIC
        mActivePacingStrategy = pacingStrategy

        val pipelineItem = mSpinnerPipelineMode?.selectedItem as? PipelineModeItem
        val pipelineMode = pipelineItem?.mode ?: "auto"
        mActivePipelineMode = pipelineMode

        stopNativeRender()
        nSetRenderApi(renderApi)
        nSetPacingStrategy(pacingStrategy)
        nSetPipelineMode(pipelineMode)
        queueCalibrations(calibrationKinds, renderApi)
        if (!mCustomScenarioActive) {
            nSelectScenario(scenarioIndex)
        }
        mCheckboxShowOverlay?.let {
            nSetOverlayEnabled(it.isChecked)
        }
        val set30Fps =
            "S5".equals(scenarioId, ignoreCase = true) || "H7".equals(scenarioId, ignoreCase = true)
        nSetOptions(true, set30Fps)

        beginTestRun()
    }

    private fun beginTestRun() {
        stopNativeRender()
        mTestActive = true
        mMainHandler.removeCallbacks(mFinishAfterAutomatedRun)
        mIsFinishingAfterAutomatedRun = false

        mSettingsLayout?.visibility = View.GONE
        mResultsLayout?.visibility = View.GONE
        hideSystemUI()
        updateBackInvokedCallbackRegistration()

        mSurfaceView?.visibility = View.VISIBLE
        val surface = mSurfaceView?.holder?.surface
        if (!mIsRendering && surface != null && surface.isValid) {
            val frame = mSurfaceView?.holder?.surfaceFrame
            val frameWidth = frame?.width() ?: 0
            val frameHeight = frame?.height() ?: 0
            val width = if (frameWidth > 0) frameWidth else (mSurfaceView?.width ?: 0)
            val height = if (frameHeight > 0) frameHeight else (mSurfaceView?.height ?: 0)
            startNativeRender(surface, width, height)
        }
    }

    private fun startNativeRender(surface: Surface?, width: Int, height: Int) {
        if (!mTestActive || mIsRendering || surface == null || !surface.isValid) {
            return
        }
        mIsRendering = true
        nStartRender(surface, width, height, true)
        mMainHandler.removeCallbacks(mPhasePollRunnable)
        mMainHandler.postDelayed(mPhasePollRunnable, PHASE_POLL_INTERVAL_MS)
    }

    private fun stopNativeRender() {
        mMainHandler.removeCallbacks(mPhasePollRunnable)
        if (mIsRendering) {
            mIsRendering = false
            nStopRender()
        }
    }

    private fun showSettingsScreen() {
        stopNativeRender()
        mTestActive = false
        mSurfaceView?.visibility = View.GONE
        mResultsLayout?.visibility = View.GONE
        mSettingsLayout?.visibility = View.VISIBLE
        updateLastResultsButton()
        updateCalibrationStatusText()
        updateBackInvokedCallbackRegistration()
    }

    private fun showLastTestResultsScreen() {
        val snapshot = mLastReportSnapshot ?: return
        stopNativeRender()
        mTestActive = false
        mSurfaceView?.visibility = View.GONE
        mSettingsLayout?.visibility = View.GONE
        populateResultsView(snapshot)
        mResultsLayout?.visibility = View.VISIBLE
        updateBackInvokedCallbackRegistration()
    }

    private fun cancelActiveTest() {
        Log.i(APP_NAME, "Test run canceled by user back navigation.")
        mPendingCalibrationRuns.clear()
        mLastReportSnapshot = captureCanceledTestReport()
        if (mCalibrationKinds != 0) {
            mCalibrationKinds = 0
            nSetCalibrationRun(0)
            nSetRenderApi(mActiveRenderApi)
            selectRenderApi(mActiveRenderApi)
        }
        showSettingsScreen()
    }

    private fun onTestComplete() {
        if (mCalibrationKinds != 0) {
            nGetUiSnapshot(mUiSnapshot)
            if (mUiSnapshot.runFailed) mCalibrationFailures.add(mUiSnapshot.taintReason)
            if (mPendingCalibrationRuns.isNotEmpty()) {
                Log.i(APP_NAME, "Calibration run complete; starting the next one.")
                stopNativeRender()
                selectNextCalibrationRun()
                beginTestRun()
                return
            }
            nSetCalibrationRun(0)
            nSetRenderApi(mActiveRenderApi)
            selectRenderApi(mActiveRenderApi)
        }
        if (!mStartedManually) {
            mCalibrationKinds = 0
            Log.i(APP_NAME, "Automated test run complete. Exiting activity.")
            stopNativeRender()
            mTestActive = false
            if (!mIsFinishingAfterAutomatedRun) {
                mIsFinishingAfterAutomatedRun = true
                mMainHandler.postDelayed(mFinishAfterAutomatedRun, 1000)
            }
            return
        }

        Log.i(APP_NAME, "Manual test run complete. Returning to settings screen.")
        val report = captureCompletedTestReport()
        mCalibrationKinds = 0
        mLastReportSnapshot = report
        if (report.status == RUN_STATUS_FAILED) {
            showLastTestResultsScreen()
        } else {
            showSettingsScreen()
        }
    }

    private fun captureCompletedTestReport(): TestReportSnapshot {
        nGetUiSnapshot(mUiSnapshot)
        val status = when {
            mUiSnapshot.runFailed -> RUN_STATUS_FAILED
            mUiSnapshot.runTainted -> RUN_STATUS_TAINTED
            else -> RUN_STATUS_COMPLETE
        }
        if (status == RUN_STATUS_FAILED) {
            Log.e(APP_NAME, "Run FAILED: ${mUiSnapshot.taintReason}")
        }
        if (mCalibrationKinds != 0) {
            val failed = mCalibrationFailures.isNotEmpty()
            return TestReportSnapshot(
                scenario = CALIBRATION_LABEL,
                scenarioDesc = "Calibration: " + formatCalibrationSet(mRequestedCalibrations),
                renderApiLabel = getRenderApiShortLabel(),
                pacingLabel = getPacingShortLabel(mActivePacingStrategy),
                pipelineLabel = getPipelineShortLabel(mActivePipelineMode),
                status = if (failed) RUN_STATUS_FAILED else status,
                taintReason = if (failed) mCalibrationFailures.joinToString("; ") else "",
                diagnosticsSummary =
                    (if (failed) {
                        "The failed calibrations were not stored; the caches of the others " +
                            "are unaffected.\n"
                    } else {
                        "Calibrations stored; scenarios that need them reuse them.\n"
                    }) + calibrationStatusSummary()
            )
        }
        val scenario = mUiSnapshot.testScenario.ifEmpty { mActiveScenarioId }
        return TestReportSnapshot(
            scenario = scenario,
            scenarioDesc = getScenarioDisplayName(scenario),
            renderApiLabel = getRenderApiShortLabel(),
            pacingLabel = getPacingShortLabel(mActivePacingStrategy),
            pipelineLabel = getPipelineShortLabel(mActivePipelineMode),
            status = status,
            taintReason = mUiSnapshot.taintReason,
            diagnosticsSummary =
                if (status == RUN_STATUS_FAILED && mUiSnapshot.missingCalibrations != 0) {
                    "The scenario did not run: missing calibrations: " +
                        formatCalibrationSet(mUiSnapshot.missingCalibrations) +
                        ". Run them with the Calibrate buttons first.\n" +
                        calibrationStatusSummary()
                } else if (status == RUN_STATUS_FAILED) {
                    "The scenario did not run."
                } else {
                    "Run complete; score it on the host from its trace (analyze_swappy_perfetto.py)."
                }
        )
    }

    private fun captureCanceledTestReport(): TestReportSnapshot {
        nGetUiSnapshot(mUiSnapshot)
        val isCalibration = mCalibrationKinds != 0
        val scenario =
            if (isCalibration) {
                CALIBRATION_LABEL
            } else {
                mUiSnapshot.testScenario.ifEmpty { mActiveScenarioId }
            }
        val scenarioDesc =
            if (isCalibration) {
                "Calibration: " + formatCalibrationSet(mRequestedCalibrations)
            } else {
                getScenarioDisplayName(scenario)
            }
        return TestReportSnapshot(
            scenario = scenario,
            scenarioDesc = scenarioDesc,
            renderApiLabel = getRenderApiShortLabel(),
            pacingLabel = getPacingShortLabel(mActivePacingStrategy),
            pipelineLabel = getPipelineShortLabel(mActivePipelineMode),
            status = RUN_STATUS_CANCELED,
            taintReason = "",
            diagnosticsSummary = "Test was canceled by user before completion."
        )
    }

    private fun updateLastResultsButton() {
        val button = mButtonViewResults ?: return
        val snapshot = mLastReportSnapshot
        if (snapshot == null) {
            button.isEnabled = false
            button.alpha = 0.5f
            button.text = "Last Test Results (None)"
            return
        }
        button.isEnabled = true
        button.alpha = 1.0f
        if (snapshot.status == RUN_STATUS_CANCELED) {
            button.text = "Last Test Results (CANCELED)"
        } else {
            button.text = String.format(
                Locale.US,
                "Last Test Results: %s [%s • %s]",
                getShortStatusLabel(snapshot.status),
                snapshot.scenario,
                snapshot.renderApiLabel
            )
        }
    }

    private fun getShortStatusLabel(status: Int): String = when (status) {
        RUN_STATUS_CANCELED -> "CANCELED"
        RUN_STATUS_TAINTED -> "TAINTED"
        RUN_STATUS_FAILED -> "FAILED"
        else -> "COMPLETE"
    }

    private fun populateResultsView(snapshot: TestReportSnapshot) {
        mResultsTitleText?.text = String.format(
            Locale.US,
            "TEST RESULTS: %s [%s]",
            snapshot.scenario,
            snapshot.renderApiLabel
        )

        when (snapshot.status) {
            RUN_STATUS_CANCELED -> {
                mResultsVerdictBadge?.text = "CANCELED"
                mResultsVerdictBadge?.setTextColor(0xFFFFAB40.toInt())
            }
            RUN_STATUS_TAINTED -> {
                mResultsVerdictBadge?.text = "TAINTED"
                mResultsVerdictBadge?.setTextColor(0xFFFFD600.toInt())
            }
            RUN_STATUS_FAILED -> {
                mResultsVerdictBadge?.text = "FAILED"
                mResultsVerdictBadge?.setTextColor(0xFFFF5252.toInt())
            }
            else -> {
                mResultsVerdictBadge?.text = "COMPLETE"
                mResultsVerdictBadge?.setTextColor(0xFF00FF88.toInt())
            }
        }

        mResultsScenarioDesc?.text = String.format(
            Locale.US,
            "%s • API: %s • Pacing: %s • Pipeline: %s",
            snapshot.scenarioDesc,
            snapshot.renderApiLabel,
            snapshot.pacingLabel,
            snapshot.pipelineLabel
        )

        val sb = StringBuilder()
        when (snapshot.status) {
            RUN_STATUS_CANCELED -> {
                sb.append("Test was canceled by user.\n")
                sb.append("Test status: CANCELED")
            }
            RUN_STATUS_TAINTED -> {
                sb.append("WARNING: Test run was TAINTED: ")
                    .append(snapshot.taintReason)
                    .append("\n")
                sb.append("Test status: COMPLETE")
            }
            RUN_STATUS_FAILED -> {
                sb.append("ERROR: ").append(snapshot.taintReason).append("\n")
                sb.append("Test status: FAILED")
            }
            else -> {
                sb.append("Test status: COMPLETE")
            }
        }
        mResultsSummaryText?.text = sb.toString()
        mResultsDiagnosticsTable?.text = snapshot.diagnosticsSummary
    }

    // "CPU, Vulkan": the labels of the calibrations in kinds.
    private fun formatCalibrationSet(kinds: Int): String =
        (0 until CALIBRATION_KIND_COUNT)
            .filter { (kinds and calibrationBit(it)) != 0 }
            .joinToString(", ") { CALIBRATION_LABELS[it] }

    // "CPU valid • GLES missing • Vulkan invalid", as of the last snapshot.
    private fun calibrationStatusSummary(): String =
        "Calibration caches: " + (0 until CALIBRATION_KIND_COUNT).joinToString(" • ") {
            CALIBRATION_LABELS[it] + " " + when (mUiSnapshot.calibrationStatus[it]) {
                CALIBRATION_STATUS_VALID -> "valid"
                CALIBRATION_STATUS_INVALID -> "invalid"
                CALIBRATION_STATUS_MISSING -> "missing"
                else -> "unknown"
            }
        }

    private fun updateCalibrationStatusText() {
        nRefreshCalibrationStatus()
        nGetUiSnapshot(mUiSnapshot)
        mTextCalibrationStatus?.text = calibrationStatusSummary()
    }

    private fun getScenarioDisplayName(scenarioId: String?): String {
        if (mCustomScenarioActive || "CUSTOM".equals(scenarioId, ignoreCase = true)) {
            return mCustomScenarioDescription
        }
        if (scenarioId != null) {
            for (item in mScenarios) {
                if (item.id.equals(scenarioId, ignoreCase = true)) {
                    return item.displayName
                }
            }
        }
        return scenarioId ?: "S1"
    }

    private fun getRenderApiShortLabel(): String =
        if (nGetRenderApi() == RENDER_API_VULKAN) "VK" else "GLES"

    private fun getPacingShortLabel(strategy: Int): String {
        for (item in PACING_STRATEGIES) {
            if (item.strategy == strategy) {
                return item.shortLabel
            }
        }
        return "Classic"
    }

    private fun getPipelineShortLabel(mode: String?): String {
        if (mode != null) {
            for (item in PIPELINE_MODES) {
                if (item.mode.equals(mode, ignoreCase = true)) {
                    return item.shortLabel
                }
            }
        }
        return "Auto"
    }

    private fun handleBackNavigation(): Boolean {
        if (mTestActive) {
            cancelActiveTest()
            return true
        }
        if (mResultsLayout?.visibility == View.VISIBLE) {
            showSettingsScreen()
            return true
        }
        return false
    }

    private fun updateBackInvokedCallbackRegistration() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU) {
            return
        }
        val shouldIntercept =
            mTestActive || (mResultsLayout != null && mResultsLayout?.visibility == View.VISIBLE)
        if (mBackInvokedCallbackObj == null) {
            mBackInvokedCallbackObj = OnBackInvokedCallback { handleBackNavigation() }
        }
        val callback = mBackInvokedCallbackObj as OnBackInvokedCallback
        if (shouldIntercept && !mBackInvokedCallbackRegistered) {
            onBackInvokedDispatcher.registerOnBackInvokedCallback(
                OnBackInvokedDispatcher.PRIORITY_DEFAULT,
                callback
            )
            mBackInvokedCallbackRegistered = true
        } else if (!shouldIntercept && mBackInvokedCallbackRegistered) {
            onBackInvokedDispatcher.unregisterOnBackInvokedCallback(callback)
            mBackInvokedCallbackRegistered = false
        }
    }

    @Deprecated("Deprecated in Java")
    @Suppress("DEPRECATION")
    override fun onBackPressed() {
        if (handleBackNavigation()) {
            return
        }
        super.onBackPressed()
    }

    private fun extractRenderApi(intent: Intent?): Int {
        if (intent == null) {
            return -1
        }
        var raw = getStringOption(intent, "api", null)
        if (raw.isNullOrBlank()) {
            raw = getStringOption(intent, "render_api", null)
        }
        if (raw.isNullOrBlank()) {
            raw = getStringOption(intent, "backend", null)
        }
        if (!raw.isNullOrBlank()) {
            val v = raw.trim().lowercase(Locale.US)
            if (v == "vulkan" || v == "vk" || v == "1") {
                return RENDER_API_VULKAN
            }
            if (v == "gles" || v == "gl" || v == "opengl" || v == "0") {
                return RENDER_API_GLES
            }
        }
        return -1
    }

    @Suppress("DEPRECATION")
    private fun rawExtra(intent: Intent?, key: String): Any? = intent?.extras?.get(key)

    private fun getStringOption(intent: Intent?, key: String, defaultValue: String?): String? {
        if (intent == null || !intent.hasExtra(key)) return defaultValue
        val valObj = rawExtra(intent, key) ?: return defaultValue
        return valObj.toString().trim()
    }

    private fun getBooleanOption(intent: Intent?, key: String, defaultValue: Boolean): Boolean {
        if (intent == null || !intent.hasExtra(key)) return defaultValue
        val valObj = rawExtra(intent, key)
        if (valObj is Boolean) return valObj
        if (valObj is String) {
            val s = valObj.trim()
            if (s == "1" || s.equals("on", ignoreCase = true) || s.equals("true", ignoreCase = true)) return true
            if (s == "0" || s.equals("off", ignoreCase = true) || s.equals("false", ignoreCase = true)) return false
            return s.toBoolean()
        }
        return intent.getBooleanExtra(key, defaultValue)
    }

    private fun getIntOption(intent: Intent?, key: String, defaultValue: Int): Int {
        if (intent == null || !intent.hasExtra(key)) return defaultValue
        val valObj = rawExtra(intent, key)
        if (valObj is Number) return valObj.toInt()
        if (valObj is String) {
            val s = valObj.trim()
            if (s.isNotEmpty()) {
                try {
                    return s.toInt()
                } catch (_: NumberFormatException) {
                }
            }
        }
        return intent.getIntExtra(key, defaultValue)
    }

    private fun extractPipelineMode(intent: Intent?): String {
        if (intent == null) {
            return "auto"
        }
        if (intent.hasExtra("pipeline")) {
            val raw = getStringOption(intent, "pipeline", null)
            if (!raw.isNullOrBlank()) {
                val v = raw.trim().lowercase(Locale.US)
                if (v == "off" || v == "false" || v == "0") {
                    return "off"
                }
                if (v == "on") {
                    return "on"
                }
                if (v == "auto" || v == "true" || v == "1") {
                    return "auto"
                }
            }
            return if (getBooleanOption(intent, "pipeline", true)) "auto" else "off"
        }
        if (intent.hasExtra("auto_pipeline")) {
            val raw = getStringOption(intent, "auto_pipeline", null)
            if (!raw.isNullOrBlank()) {
                val v = raw.trim().lowercase(Locale.US)
                if (v == "auto" || v == "true" || v == "1") {
                    return "auto"
                }
                if (v == "on") {
                    return "on"
                }
                if (v == "off" || v == "false" || v == "0") {
                    return "off"
                }
            }
            return if (getBooleanOption(intent, "auto_pipeline", true)) "auto" else "off"
        }
        return "auto"
    }

    private fun applyGlobalSwappyIntentExtras(intent: Intent) {
        val requestedApi = extractRenderApi(intent)
        if (requestedApi >= 0) {
            nSetRenderApi(requestedApi)
        }

        val pacing = getStringOption(intent, "pacing", null)
        if (!pacing.isNullOrEmpty()) {
            var strategy = PACING_CLASSIC
            if (pacing.equals("no_swappy", ignoreCase = true) ||
                pacing.equals("noswappy", ignoreCase = true) ||
                pacing.equals("none", ignoreCase = true) ||
                pacing == "3"
            ) {
                strategy = PACING_NO_SWAPPY
            }
            mActivePacingStrategy = strategy
            nSetPacingStrategy(strategy)
        } else if (intent.hasExtra("pacing_strategy")) {
            val requested = getIntOption(intent, "pacing_strategy", PACING_CLASSIC)
            val strategy = if (requested == PACING_NO_SWAPPY) PACING_NO_SWAPPY else PACING_CLASSIC
            mActivePacingStrategy = strategy
            nSetPacingStrategy(strategy)
        }

        val pipelineMode = extractPipelineMode(intent)
        mActivePipelineMode = pipelineMode
        nSetPipelineMode(pipelineMode)

        if (intent.hasExtra("auto_swap_interval") || intent.hasExtra("auto_si")) {
            val autoSi =
                if (intent.hasExtra("auto_swap_interval")) {
                    getBooleanOption(intent, "auto_swap_interval", true)
                } else {
                    getBooleanOption(intent, "auto_si", true)
                }
            nSetAutoSwapInterval(autoSi)
        }

        val stuffFix =
            getIntOption(intent, "stuff_fix", getIntOption(intent, "buffer_stuffing_fix", -1))
        if (stuffFix >= 0) {
            nSetBufferStuffingFixWait(stuffFix)
        }

        val fenceTimeoutMs = getIntOption(intent, "fence_timeout_ms", -1)
        if (fenceTimeoutMs >= 0) {
            nSetFenceTimeoutMs(fenceTimeoutMs)
        }
    }

    private fun extractCustomScenarioSpec(intent: Intent?): String? {
        if (intent == null || !intent.hasExtra("custom")) return null
        return getStringOption(intent, "custom", null) ?: ""
    }

    private fun hideSystemUI() {
        val decorView = window.peekDecorView() ?: window.decorView ?: return

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            val controller = decorView.windowInsetsController ?: window.insetsController
            controller?.let {
                it.hide(WindowInsets.Type.systemBars())
                it.systemBarsBehavior =
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            decorView.systemUiVisibility =
                (View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                    or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                    or View.SYSTEM_UI_FLAG_FULLSCREEN)
        }
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            hideSystemUI()
        }
    }

    override fun onNewIntent(intent: Intent?) {
        super.onNewIntent(intent)
        setIntent(intent)
        applyIntentAndRouteScreen(intent)
    }

    override fun onDestroy() {
        super.onDestroy()
        mMainHandler.removeCallbacks(mPhasePollRunnable)
        mMainHandler.removeCallbacks(mFinishAfterAutomatedRun)
        stopNativeRender()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            mBackInvokedCallbackRegistered &&
            mBackInvokedCallbackObj != null
        ) {
            onBackInvokedDispatcher.unregisterOnBackInvokedCallback(
                mBackInvokedCallbackObj as OnBackInvokedCallback
            )
            mBackInvokedCallbackRegistered = false
        }
    }

    override fun onCreateOptionsMenu(menu: Menu?): Boolean = false

    override fun surfaceCreated(holder: SurfaceHolder) {
        if (mTestActive && !mIsRendering) {
            var width = holder.surfaceFrame.width()
            if (width <= 0) width = mSurfaceView?.width ?: 0
            var height = holder.surfaceFrame.height()
            if (height <= 0) height = mSurfaceView?.height ?: 0
            startNativeRender(holder.surface, width, height)
        }
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        nSetSurfaceSize(width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        stopNativeRender()
    }

    companion object {
        private const val APP_NAME = "SwappyTestActivity"
        private const val PHASE_POLL_INTERVAL_MS = 250L

        // Mirrors the TestPhase enum in DvfsBenchmark.h.
        private const val TEST_PHASE_READY = -1
        private const val TEST_PHASE_COMPLETE = 4

        // How the last run ended. It is scored on the host from its trace.
        private const val RUN_STATUS_CANCELED = -1
        private const val RUN_STATUS_COMPLETE = 0
        private const val RUN_STATUS_TAINTED = 1
        // The run produced no result, e.g. a scenario started without a
        // stored calibration; the taint reason says why.
        private const val RUN_STATUS_FAILED = 2

        // The scenario label of calibration-action reports.
        private const val CALIBRATION_LABEL = "CALIBRATION"

        // Mirrors the SwappyRenderApi enum in DvfsBenchmark.h.
        private const val RENDER_API_GLES = 0
        private const val RENDER_API_VULKAN = 1

        // Mirrors the CalibrationKind enum in CalibrationKind.h.
        private const val CALIBRATION_CPU = 0
        private const val CALIBRATION_GLES = 1
        private const val CALIBRATION_VULKAN = 2
        private const val CALIBRATION_KIND_COUNT = 3
        private const val ALL_CALIBRATIONS = (1 shl CALIBRATION_KIND_COUNT) - 1
        private val CALIBRATION_LABELS = arrayOf("CPU", "GLES", "Vulkan")

        // Mirrors the CalibrationStatus enum in CalibrationKind.h.
        private const val CALIBRATION_STATUS_MISSING = 0
        private const val CALIBRATION_STATUS_INVALID = 1
        private const val CALIBRATION_STATUS_VALID = 2

        private fun calibrationBit(kind: Int): Int = 1 shl kind

        private fun gpuCalibrationKind(api: Int): Int =
            if (api == RENDER_API_VULKAN) CALIBRATION_VULKAN else CALIBRATION_GLES

        // Classic Swappy pacing. Must match kPacingClassic in
        // SwappyTestRenderer.cpp.
        private const val PACING_CLASSIC = 0
        // Sample-only baseline: Swappy is never initialized and frames are
        // presented with raw eglSwapBuffers / vkQueuePresentKHR. Must match
        // kPacingNoSwappy in SwappyTestRenderer.cpp.
        private const val PACING_NO_SWAPPY = 3

        val RENDER_APIS = arrayOf(
            RenderApiItem(RENDER_API_GLES, "GLES", "OpenGL ES (SwappyGL)"),
            RenderApiItem(RENDER_API_VULKAN, "VK", "Vulkan (SwappyVk)")
        )

        val PACING_STRATEGIES = arrayOf(
            PacingStrategyItem(PACING_CLASSIC, "Classic", "Classic (Measured Frame Time)"),
            PacingStrategyItem(PACING_NO_SWAPPY, "NoSwappy", "No Swappy (Raw Swap / Present)")
        )

        val PIPELINE_MODES = arrayOf(
            PipelineModeItem("auto", "Auto", "Auto"),
            PipelineModeItem("on", "On", "On (Pipelined)"),
            PipelineModeItem("off", "Off", "Off (Non-Pipelined)")
        )

        init {
            System.loadLibrary("swappytest")
        }

        @JvmStatic
        external fun nSetRenderApi(api: Int)

        @JvmStatic
        external fun nGetRenderApi(): Int

        @JvmStatic
        external fun nSetOptions(swappyEnabled: Boolean, set30FpsLimit: Boolean)

        @JvmStatic
        external fun nSetPipelineMode(mode: String)

        @JvmStatic
        external fun nSetAutoSwapInterval(enabled: Boolean)

        @JvmStatic
        external fun nSetBufferStuffingFixWait(nFrames: Int)

        @JvmStatic
        external fun nSetFenceTimeoutMs(timeoutMs: Int)

        @JvmStatic
        external fun nSetPacingStrategy(strategy: Int)

        @JvmStatic
        external fun nStartRender(surface: Surface, width: Int, height: Int, autoStart: Boolean)

        @JvmStatic
        external fun nStopRender()

        @JvmStatic
        external fun nSetSurfaceSize(width: Int, height: Int)

        @JvmStatic
        external fun nGetUiSnapshot(out: UiSnapshot)

        @JvmStatic
        external fun nSetTestScenario(scenario: String)

        @JvmStatic
        external fun nSelectScenario(scenarioIndex: Int)

        @JvmStatic
        external fun nSetCustomScenario(spec: String)

        @JvmStatic
        external fun nSetRunIteration(iteration: Int)

        @JvmStatic
        external fun nGetScenarioCount(): Int

        @JvmStatic
        external fun nGetScenarioId(scenarioIndex: Int): String?

        @JvmStatic
        external fun nGetScenarioDescription(scenarioIndex: Int): String?

        @JvmStatic
        external fun nSetOutputDirectory(dirPath: String)

        // kinds: the CalibrationKind bits to calibrate; 0 runs the scenario.
        @JvmStatic
        external fun nSetCalibrationRun(kinds: Int)

        @JvmStatic
        external fun nRefreshCalibrationStatus()

        @JvmStatic
        external fun nSetOverlayEnabled(enabled: Boolean)

        @JvmStatic
        external fun nOnTouchEvent(action: Int, x: Float, y: Float, eventTimeNs: Long, isManual: Boolean)
    }
}
