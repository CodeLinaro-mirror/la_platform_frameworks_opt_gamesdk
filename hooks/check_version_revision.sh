#!/bin/bash

latest_commit_sha=$(git rev-parse HEAD)

# When we update VERSIONS file we should update the version revision macros where necessary.
if git show --pretty="" --name-only HEAD | grep -q VERSIONS; then
  echo "You need to update the version revision macros for the libraries you are updating."
  echo ""
  echo "Run the command:"
  echo ""
  echo ""
  echo "  ./revision.sh $latest_commit_sha"
  echo ""
  echo ""
  echo "To fix."
fi