# Repository Guidelines

## Project Structure & Module Organization

OpenDisplay contains two native apps and a landing site. `Mac/` implements the macOS sender; `iOS/` implements the receiver and video renderer. Shared target code lives in `Shared/`, especially the wire protocol. XcodeGen reads `project.yml`; generated `OpenSidecar.xcodeproj` and `Mac/Info.plist`/`iOS/Info.plist` must not be edited or committed. The React/Vite site lives in `src/`, with static assets in `public/` and helpers in `tools/`. Release automation is under `fastlane/` and `.github/workflows/`.

## Build, Test, and Development Commands

- `./generate.sh` loads the optional `.env` signing team and regenerates the Xcode project. Install XcodeGen first.
- `xcodebuild -project OpenSidecar.xcodeproj -scheme OpenSidecarMac -configuration Debug -derivedDataPath build build` builds the macOS sender.
- `xcodebuild -project OpenSidecar.xcodeproj -scheme OpenSidecariOS -configuration Debug -destination 'generic/platform=iOS' -derivedDataPath build -allowProvisioningUpdates build` builds the receiver.
- `./run.sh` opens the already-built Debug macOS app.
- `pnpm install --frozen-lockfile` installs the pinned website dependencies.
- `pnpm dev`, `pnpm build`, and `pnpm preview` start the site, produce the prerendered `docs/` output, and preview it locally.

## Coding Style & Naming Conventions

Follow existing formatting: four-space indentation in Swift and two spaces in TypeScript/TSX, with semicolon-free frontend code. Use `UpperCamelCase` for Swift types and React components, `lowerCamelCase` for variables/functions, and filenames matching the primary type or component. Keep platform-specific behavior in its platform directory and protocol changes in `Shared/`. TypeScript is strict; `pnpm build` performs compilation checks. No formatter or linter is configured, so preserve nearby style.

## Testing Guidelines

There is currently no committed automated test target or coverage threshold. Before submitting, build every affected native scheme and run `pnpm build` for website changes. Exercise transport or rendering changes with the iOS receiver and macOS sender; `tools/fake-receiver.swift` is available for focused sender diagnostics. Document tested devices, OS versions, USB/WiFi paths, and permission states in the PR.

## Commit & Pull Request Guidelines

Use Conventional Commits seen in history, optionally scoped: `feat(mac): ...`, `fix(ios): ...`, `docs: ...`, or `perf: ...`. Keep commits narrowly focused; release automation derives changelogs and versions from them. PRs should explain behavior and motivation, link relevant issues, list validation performed, and include screenshots or recordings for UI changes. Call out compatibility, protocol, entitlement, or private-API impacts explicitly.

## Security & Configuration

Keep signing values in ignored `.env` files (for example, `DEVELOPMENT_TEAM=...`). Never commit certificates, provisioning profiles, App Store credentials, or Sparkle private keys.
