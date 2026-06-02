/*
 * Copyright (C) 2026 The Android Open Source Project
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

package com.google.androidgamesdk

import org.gradle.api.DefaultTask
import org.gradle.api.Project
import org.gradle.api.file.RegularFileProperty
import org.gradle.api.provider.ListProperty
import org.gradle.api.tasks.Input
import org.gradle.api.tasks.Nested
import org.gradle.api.tasks.OutputFile
import org.gradle.api.tasks.TaskAction
import java.io.File

data class PublishedArtifact(
    @get:Input val artifactPath: String,
    @get:Input val sbomPath: String
) {
    fun toJsonString(): String = """
        {
          "artifact_path": "$artifactPath",
          "sbom_path": "$sbomPath",
          "attest_archive_contents": true
        }
    """.trimIndent()
}

fun registerAttestationManifestTask(project: Project, libraries: List<AndroidArchiveLibrary>, distPath: String, pkgName: String) {
    project.tasks.register("generateAttestationManifest", GenerateAttestationManifest::class.java) {
        group = "distribution"
        description = "Generates a JSON manifest file listing artifact and SBOM paths for all subprojects."

        outputFile.set(File("$distPath/attestation_manifest.json"))
        artifacts.set(libraries.map { it.toPublishedArtifact(pkgName, version = it.libraryInfo.version.toString()) })
    }
}

abstract class GenerateAttestationManifest : DefaultTask() {

    @get:OutputFile
    abstract val outputFile: RegularFileProperty
    @get:Nested
    abstract val artifacts: ListProperty<PublishedArtifact>

    @TaskAction
    fun generate() {
        val entries = artifacts.get()
        val jsonListString = entries.joinToString(
            separator = ",\n",
            prefix = "[\n",
            postfix = "\n]"
        ) { entry ->
            entry.toJsonString().prependIndent("  ")
        }

        val file = outputFile.get().asFile
        file.writeText(jsonListString)
    }
}

private fun AndroidArchiveLibrary.toPublishedArtifact(packageName: String, version: String) = PublishedArtifact(
    artifactPath = projectZipPath(packageName),
    sbomPath = sbomPath(version)
)

private fun AndroidArchiveLibrary.sbomPath(version: String): String = "sboms/${this.projectName}-${version}.spdx.json"