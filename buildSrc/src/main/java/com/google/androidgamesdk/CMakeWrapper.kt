package com.google.androidgamesdk

import java.io.File
import org.gradle.api.Project

class CMakeWrapper {
    companion object {
        private fun ensureFoldersReady(
            project: Project,
            buildFolders: BuildFolders
        ) {
            project.mkdir(buildFolders.workingFolder)
            project.mkdir(buildFolders.outputFolder)
        }

        /**
         * Run CMake on the specified folders using the specified Android
         * toolchain and build options.
         * Flags are set to only compile the specified libraries.
         *
         * In case of error, a verbose exception is thrown to help pinpoint
         * the configuration that led to the error.
         */
        @JvmStatic
        fun runAndroidCMake(
            project: Project,
            buildFolders: BuildFolders,
            toolchain: Toolchain,
            buildOptions: BuildOptions,
            libraries: Collection<NativeLibrary>,
            gitCommit: String
        ) {
            ensureFoldersReady(project, buildFolders)

            val ndkPath = toolchain.getAndroidNDKPath()
            val toolchainFilePath = ndkPath +
                "/build/cmake/android.toolchain.cmake"
            val androidVersion = toolchain.getAndroidVersion()

            var cxx_flags = "-DANDROID_NDK_VERSION=${toolchain.getNdkVersionNumber()} " +
            "-DAGDK_GIT_COMMIT=${gitCommit}"

            if (buildOptions.stl == "gnustl_static" ||
                buildOptions.stl == "gnustl_shared"
            )
                cxx_flags += " -DANDROID_GNUSTL"

            val cmdLine = mutableListOf(
                toolchain.getCMakePath(),
                buildFolders.projectFolder,
                "-DCMAKE_BUILD_TYPE=" + buildOptions.buildType,
                "-DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=" + buildFolders.outputFolder,
                "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=" + buildFolders.outputFolder,
                "-DGAMESDK_THREAD_CHECKS=" +
                    (if (buildOptions.threadChecks) "1" else "0"),
                "-DCMAKE_MAKE_PROGRAM=" + toolchain.getNinjaPath(),
                "-GNinja"
            )

            if (buildOptions.arch != "host") {
                cmdLine.add("-DCMAKE_CXX_FLAGS=$cxx_flags")
                cmdLine.addAll(listOf(
                    "-DANDROID_PLATFORM=android-$androidVersion",
                    "-DCMAKE_ANDROID_NDK=$ndkPath",
                    "-DANDROID_STL=" + buildOptions.stl,
                    "-DANDROID_ABI=" + buildOptions.arch,
                    "-DANDROID_UNIFIED_HEADERS=1",
                    "-DCMAKE_C_COMPILER_WORKS=1",
                    "-DCMAKE_CXX_COMPILER_WORKS=1",
                    "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY",
                    "-DCMAKE_ANDROID_NDK_TOOLCHAIN_VERSION=clang",
                    "-DCMAKE_ANDROID_STL_TYPE=" + buildOptions.stl,
                    "-DCMAKE_TOOLCHAIN_FILE=$toolchainFilePath"
                ))
            } else {
                val sysCc = System.getenv("CC")
                val sysCxx = System.getenv("CXX")
                if (sysCc.isNullOrEmpty() || sysCxx.isNullOrEmpty()) {
                    val clangC = toolchain.findNDKTool("clang")
                    val clangCxx = toolchain.findNDKTool("clang++")
                    cmdLine.add("-DCMAKE_C_COMPILER=$clangC")
                    cmdLine.add("-DCMAKE_CXX_COMPILER=$clangCxx")

                    val llvmRoot = File(clangCxx).parentFile?.parentFile
                    val hostInclude = File(llvmRoot, "sysroot/usr/include/c++/v1")
                    val hostLib = File(llvmRoot, "lib/x86_64-unknown-linux-gnu")

                    var hostCxxFlags = "$cxx_flags -stdlib=libc++"
                    var hostLinkerFlags = "-stdlib=libc++ -static-libstdc++"
                    if (hostInclude.exists() && hostLib.exists()) {
                        // When compiling for host GNU/Linux using NDK Clang, headers default to
                        // the host system GCC/libstdc++ headers. Provide the NDK LLVM libc++
                        // headers directly via -isystem.
                        // Suppress __config_site to override _LIBCPP_ABI_NAMESPACE from __ndk1
                        // back to __1, matching the host static library libc++.a symbols.
                        hostCxxFlags += " -isystem ${hostInclude.path} " +
                            "-D_LIBCPP___CONFIG_SITE " +
                            "-D_LIBCPP_ABI_VERSION=1 " +
                            "-D_LIBCPP_ABI_NAMESPACE=__1 " +
                            "-D_LIBCPP_HAS_NO_VENDOR_AVAILABILITY_ANNOTATIONS " +
                            "-D_LIBCPP_HARDENING_MODE_DEFAULT=2 " +
                            "-D_LIBCPP_PSTL_CPU_BACKEND_THREAD"
                        hostLinkerFlags += " -L${hostLib.path}"
                    }
                    cmdLine.add("-DCMAKE_CXX_FLAGS=$hostCxxFlags")
                    cmdLine.add("-DCMAKE_EXE_LINKER_FLAGS=$hostLinkerFlags")
                } else {
                    cmdLine.add("-DCMAKE_C_COMPILER=$sysCc")
                    cmdLine.add("-DCMAKE_CXX_COMPILER=$sysCxx")
                    // Force the host compiler to use libc++ to avoid missing modern C++ features
                    // in older system libstdc++ implementations (like in the Busytown Docker).
                    cmdLine.add("-DCMAKE_CXX_FLAGS=$cxx_flags -stdlib=libc++ -static-libstdc++")
                    cmdLine.add("-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++")
                }
            }

            if (!libraries.isEmpty()) {
                cmdLine.add("-DGAMESDK_LIBRARIES=" + libraries.joinToString(";") {
                    nativeLibrary -> nativeLibrary.nativeLibraryName
                })
            }

            val out = java.io.ByteArrayOutputStream()
            try {
                project.exec {
                    val protocBinDir = toolchain.getProtobufInstallPath() +
                        "/bin"
                    environment(
                        "PATH",
                        protocBinDir + ":" +
                            environment.get("PATH")
                    )
                    workingDir(buildFolders.workingFolder)
                    commandLine(cmdLine)
                    standardOutput = out
                    errorOutput = out
                }
            } catch (cmakeException: Throwable) {
                val libraryNames = libraries.map { it.nativeLibraryName }
                    .joinToString()
                throw Exception(
                    "Error when running CMake for " +
                        (if (libraryNames.isEmpty()) buildFolders.toString() else libraryNames) + " with " +
                        toolchain + " and " + buildOptions + "\nCMake output:\n" + out.toString(),
                    cmakeException
                )
            }
        }

        /**
         * Run Ninja, after CMake was
         * run in a folder (@see runAndroidCMake).
         */
        @JvmStatic
        fun runNinja(
            project: Project,
            toolchain: Toolchain,
            workingFolder: String
        ) {
            val out = java.io.ByteArrayOutputStream()
            try {
                project.exec {
                    workingDir(workingFolder)
                    commandLine(mutableListOf(toolchain.getNinjaPath()))
                    standardOutput = out
                    errorOutput = out
                }
            } catch (makeException: Throwable) {
                throw Exception(
                    "Error when building with " +
                        toolchain + " in " + workingFolder + "\nNinja output:\n" + out.toString(),
                    makeException
                )
            }
        }
    }
}
