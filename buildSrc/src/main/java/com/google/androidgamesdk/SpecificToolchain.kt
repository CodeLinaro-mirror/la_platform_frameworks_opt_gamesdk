package com.google.androidgamesdk

import org.gradle.api.Project
import java.io.File

class SpecificToolchain(
    override var project_: Project,
    override var androidVersion_: String,
    override var ndkVersion_: String
) : Toolchain() {
    override fun getAndroidNDKPath(): String {
        val specificNdk = File(prebuiltsDir, "ndk/$ndkVersion_")
        if (specificNdk.exists()) return specificNdk.path
        val currentNdk = File(prebuiltsDir, "ndk/current")
        if (currentNdk.exists()) return currentNdk.path
        return specificNdk.path
    }
}
