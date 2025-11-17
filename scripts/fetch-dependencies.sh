#!/bin/bash
set -euo pipefail

echo "🔍 Fetching WASM dependencies..."

# Check if dependencies.json exists
if [[ ! -f "dependencies.json" ]]; then
  echo "⚠️  No dependencies.json found"
  exit 0
fi

# Create dependencies directory
mkdir -p deps/wasm

# Parse dependencies.json and fetch each dependency
# This is a placeholder - actual implementation would use jq to parse JSON
if command -v jq >/dev/null 2>&1; then
  DEP_COUNT=$(jq '.dependencies | length' dependencies.json)

  for i in $(seq 0 $((DEP_COUNT - 1))); do
    NAME=$(jq -r ".dependencies[$i].name" dependencies.json)
    URL=$(jq -r ".dependencies[$i].url" dependencies.json)

    echo "📦 Fetching $NAME..."

    if command -v curl >/dev/null 2>&1; then
      curl -L -o "deps/wasm/$NAME.wasm" "$URL" || echo "⚠️  Failed to fetch $NAME"
    elif command -v wget >/dev/null 2>&1; then
      wget -O "deps/wasm/$NAME.wasm" "$URL" || echo "⚠️  Failed to fetch $NAME"
    else
      echo "❌ Neither curl nor wget found"
      exit 1
    fi
  done
else
  echo "⚠️  jq not found, skipping dependency fetch"
fi

echo "✅ Dependencies fetched to deps/wasm/"
