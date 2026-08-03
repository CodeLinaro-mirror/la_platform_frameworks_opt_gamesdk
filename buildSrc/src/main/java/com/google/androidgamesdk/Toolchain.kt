package com.google.androidgamesdk

import com.google.androidgamesdk.OsSpecificTools.Companion.joinPath
import com.google.androidgamesdk.OsSpecificTools.Companion.osExecutableSuffix
import com.google.androidgamesdk.OsSpecificTools.Companion.osFolderName
import org.gradle.api.GradleException
import org.gradle.api.Project
import java.io.File
import java.util.Properties
import java.util.regex.Pattern

abstract class Toolchain {
    protected abstract var project_: Project
    protected abstract var androidVersion_: String
    protected abstract var ndkVersion_: String

    override fun toString(): String {
        return "Toolchain(androidVersion=$androidVersion_, " +
            "ndkVersion=$ndkVersion_)"
    }

    abstract fun getAndroidNDKPath(): String

    fun getAndroidVersion(): String {
        return androidVersion_
    }

    fun getNdkVersion(): String {
        return ndkVersion_
    }

    fun getNdkVersionNumber(): String {
        return extractNdkMajorVersion(ndkVersion_)
    }

    fun getBuildKey(buildOptions: BuildOptions): String {
        return buildOptions.arch + "_API" + androidVersion_ +
            "_NDK" + getNdkVersionNumber() + '_' +
            sanitize(buildOptions.stl) + '_' + buildOptions.buildType
    }

    protected fun getNdkVersionFromPropertiesFile(): String {
        val file = File(getAndroidNDKPath(), "source.properties")
        if (!file.exists()) {
            println("Warning: can't get NDK version from " + getAndroidNDKPath()+ "/source.properties")
            return "UNKNOWN"
        } else {
            val props = loadPropertiesFromFile(file)
            val ver = props["Pkg.Revision"]
            if (ver is String) {
                return extractNdkMajorVersion(ver)
            }
            /* ktlint-disable max-line-length */
            println("Warning: can't get NDK version from source.properties (Pkg.Revision is not a string)")
            /* ktlint-enable max-line-length */
            return "UNKNOWN"
        }
    }

    private fun sanitize(s: String): String {
        return s.replace('+', 'p')
    }

    fun findNDKTool(name: String): String {
        val searchedPath = joinPath(getAndroidNDKPath(), "toolchains")
        val tools = project_.fileTree(searchedPath).matching {
            include("**/bin/" + name + osExecutableSuffix())
            // Be sure not to use tools that would not support architectures
            // for which we're building:
            exclude("**/aarch64-*", "**/arm-*")
        }
        if (tools.isEmpty) {
            throw GradleException("No $name found in $searchedPath")
        }
        return tools.files.last().toString()
    }

    fun getArPath(): String {
        return findNDKTool("ar")
    }

    val prebuiltsDir: File by lazy {
        val envPrebuilts = System.getenv("PREBUILTS_DIR")
        if (!envPrebuilts.isNullOrEmpty() && File(envPrebuilts).exists()) {
            File(envPrebuilts)
        } else {
            val standalone = File(project_.projectDir, "../prebuilts")
            if (standalone.exists()) standalone
            else {
                val aosp = File(project_.projectDir, "../../../prebuilts")
                if (aosp.exists()) aosp else standalone
            }
        }
    }

    val isAospCheckout: Boolean by lazy {
        System.getenv("IS_AOSP_CHECKOUT")?.toBoolean() ?:
            (!File(project_.projectDir, "../prebuilts").exists() &&
                File(project_.projectDir, "../../../prebuilts").exists())
    }

    fun getCMakePath(): String {
        val sdkCmake = File(System.getenv("ANDROID_HOME") ?: "", "cmake/3.22.1/bin/cmake" + osExecutableSuffix())
        if (sdkCmake.exists()) return sdkCmake.path
        val prebuiltCmake = File(prebuiltsDir, "cmake/" + osFolderName(ExternalToolName.CMAKE) + "/bin/cmake" + osExecutableSuffix())
        if (prebuiltCmake.exists()) return prebuiltCmake.path
        return "cmake"
    }

    fun getNinjaPath(): String {
        val sdkNinja = File(System.getenv("ANDROID_HOME") ?: "", "cmake/3.22.1/bin/ninja" + osExecutableSuffix())
        if (sdkNinja.exists()) return sdkNinja.path
        val prebuiltNinja = File(prebuiltsDir, "ninja/" + osFolderName(ExternalToolName.CMAKE) + "/ninja" + osExecutableSuffix())
        if (prebuiltNinja.exists()) return prebuiltNinja.path
        val aospNinja = File(prebuiltsDir, "build-tools/" + osFolderName(ExternalToolName.CMAKE) + "/bin/ninja" + osExecutableSuffix())
        if (aospNinja.exists()) return aospNinja.path
        return "ninja"
    }

    fun getProtobufInstallPath(): String {
        return File(
            "${project_.projectDir}/third_party/protoc-3.21.7/" +
                osFolderName(ExternalToolName.PROTOBUF)
        ).path
    }

    protected fun extractNdkMajorVersion(ndkVersion: String): String {
        val majorVersionPattern = Pattern.compile("[0-9]+")
        val matcher = majorVersionPattern.matcher(ndkVersion)
        if (matcher.find()) {
            return matcher.group()
        }
        return "UNKNOWN"
    }

    protected fun loadPropertiesFromFile(file: File): Properties {
        val props = Properties()
        val inputStream = file.inputStream()
        props.load(inputStream)
        inputStream.close()

        return props
    }
}
