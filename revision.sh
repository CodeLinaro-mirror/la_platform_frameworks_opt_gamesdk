#!/bin/bash

commit_sha=$1

declare -A lib_gfp=(
  [name]="games-frame-pacing"
  [header]="include/swappy/swappy_common.h"
  [prefix]="SWAPPY"
)

declare -A lib_gpt=(
  [name]="games-performance-tuner"
  [header]="include/tuningfork/tuningfork.h"
  [prefix]="TUNINGFORK"
)

declare -A lib_ga=(
  [name]="game-activity"
  [header]="game-activity/prefab-src/modules/game-activity/include/game-activity/GameActivity.h"
  [prefix]="GAMEACTIVITY"
)

declare -A lib_gti=(
  [name]="game-text-input"
  [header]="game-text-input/prefab-src/modules/game-text-input/include/game-text-input/gametextinput.h"
  [prefix]="GAMETEXTINPUT"
)

declare -A lib_gc=(
  [name]="games-controller"
  [header]="games-controller/src/main/cpp/paddleboat/include/paddleboat.h"
  [prefix]="PADDLEBOAT"
)

declare -A lib_ma=(
 [name]="games-memory-advice"
 [header]="include/memory_advice/memory_advice.h"
 [prefix]="MEMORY_ADVICE"
)

libraries=(
  lib_gfp
  lib_gpt
  lib_ga
  lib_gti
  lib_gc
  lib_ma
)

for lib_name in "${libraries[@]}"; do
  declare -n struct="$lib_name"

  name="${struct[name]}"
  header="${struct[header]}"
  prefix="${struct[prefix]}"
  version_pattern="${prefix}_MAJOR_VERSION"
  macro_pattern="${prefix}_VERSION_REVISION"
  macro="#define ${macro_pattern} $commit_sha"

  # If we have updated the version of this library
  if git diff HEAD~1 HEAD -- VERSIONS | grep -E "^\+" | grep -q "$name"; then
    if grep -q "$macro_pattern" "$header"; then
      # If revision macro already exists, replace it.
      sed -i "s|^#define ${macro_pattern}.*|$macro|" "$header"
    else
      # Find line number of version macro.
      line_number=$(grep -n -m 1 "$version_pattern" "$header" | cut -d: -f1)
      if [ -z "$line_number" ]; then
          echo "Error: Pattern \"${version_pattern}\" not present in file: \"${header}\""
          exit 1
      fi

      # Insert macro at line number.
      sed -i "${line_number}i $macro" "$header"
    fi
  fi
done
