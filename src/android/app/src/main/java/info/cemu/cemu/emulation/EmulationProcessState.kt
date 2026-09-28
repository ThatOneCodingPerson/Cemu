package info.cemu.cemu.emulation

/**
 * Emulator state of this (emulation) process. The native core can only run one title per process, and
 * it keeps running when its EmulationActivity is replaced (e.g. a launcher shortcut starts a new one with
 * FLAG_ACTIVITY_CLEAR_TASK). A new activity then attaches to the running title instead of launching again.
 * Main thread only.
 */
object EmulationProcessState {
    /** Launch path of the title running in this process, null until a launch succeeded. */
    var runningGamePath: String? = null
}
