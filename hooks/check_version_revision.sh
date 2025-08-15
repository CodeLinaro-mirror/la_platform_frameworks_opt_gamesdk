#!/bin/bash

latest_commit_sha=$(git ls-remote https://android.googlesource.com/platform/frameworks/opt/gamesdk/ gamesdk-main | awk '{print $1}')

# When we update VERSIONS file we should update the version revision macros.
if ! git diff --quiet -- "VERSIONS"; then
  # Only need to check one header instead of all.
  sha_in_file=$(grep "GAMEACTIVITY_VERSION_REVISION" "game-activity/prefab-src/modules/game-activity/include/game-activity/GameActivity.h" | awk '{print $3}')
  if [[ "$sha_in_file" != "$latest_commit_sha" ]]; then
    echo "Version revision mismatch! The version revision defined in the header files is:"
    echo "  $sha_in_file"
    echo "It should be:"
    echo "  $latest_commit_sha"
    echo ""
    echo "Run the command:"
    echo ""
    echo ""
    echo "  ./revision.sh $latest_commit_sha"
    echo ""
    echo ""
    echo "To fix."

    exit 1
  fi
fi