# Release Workflow

When you want a release, tell Claude "do a release" or "cut a release".

## Steps

### 1. Consume changesets and bump version
```bash
npx changeset version
```
Reads `.changeset/*.md` files, bumps `version` in `package.json`, writes `CHANGELOG.md`.

### 2. Sync FW_VERSION in config.h
Update `FW_VERSION` in `src/config.h` to match the new version from `package.json`.

### 3. Build
```bash
~/.platformio/penv/bin/pio run
```

### 4. Commit the version bump
Stage and commit: `package.json`, `CHANGELOG.md`, `src/config.h`, deleted changeset files.

### 5. Tag and push
```bash
git tag v<version>
git push && git push --tags
```

### 6. Create GitHub release with firmware binary
```bash
gh release create v<version> .pio/build/esp32c3/firmware.bin --title "v<version>" --notes "<changelog entry>"
```

### 7. OTA deploy (if requested)
From HA Developer Tools → Services → mqtt.publish:
- **Topic:** `rainbird/ota/set`
- **Payload:** `https://github.com/maillme/rainbird-esp32/releases/download/v<version>/firmware.bin`
