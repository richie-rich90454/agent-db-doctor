# Packaging

The manifests for winget and for a personal Homebrew tap, and the two scripts
that fill them in. The download URLs and the installer hashes can only exist once
a release exists, so everything here ships as a template with `{{VERSION}}`,
`{{URL}}`, and `{{SHA256}}` placeholders, and the scripts write the finished file
beside the template. Nothing here runs during a build; it is all release day work.

Release 1.0.0 is published: the Homebrew formula is in
`richie-rich90454/homebrew-tap`, and the winget manifests are in review at
[microsoft/winget-pkgs#456765](https://github.com/microsoft/winget-pkgs/pull/456765).

| What | Template | Script | Finished file |
|---|---|---|---|
| winget version manifest | `winget/agent-db-doctor.yaml.template` | `generate-winget-manifests.ps1` | `winget/richie-rich90454.agent-db-doctor.yaml` |
| winget locale manifest | `winget/agent-db-doctor.locale.en-US.yaml.template` | `generate-winget-manifests.ps1` | `winget/richie-rich90454.agent-db-doctor.locale.en-US.yaml` |
| winget installer manifest | `winget/agent-db-doctor.installer.yaml.template` | `generate-winget-manifests.ps1` | `winget/richie-rich90454.agent-db-doctor.installer.yaml` |
| Homebrew formula | `homebrew/agent-db-doctor.rb.template` | `generate-homebrew-formula.ps1` | `homebrew/agent-db-doctor.rb` |

## winget

The Windows Package Manager Community Repository (`microsoft/winget-pkgs`) accepts
a package from any GitHub account. There is no registration, no fee, and no
account beyond the one needed to open the pull request; the account that opens the
first submission becomes the maintainer of the package under that identifier. The
package identifier here is `richie-rich90454.agent-db-doctor`, which requires the
pull request to come from the `richie-rich90454` account.

Release day, once `agent-db-doctor-<version>-windows-x86_64.zip` is attached to
the release:

```powershell
# 1. Fill the templates from the downloaded zip.
.\packaging\generate-winget-manifests.ps1 -Version 1.0.0 `
    -ZipPath .\agent-db-doctor-1.0.0-windows-x86_64.zip

# 2. Check the three finished files against the schema.
winget validate --manifest .\packaging\winget

# 3. Let wingetcreate fork the repository and open the pull request. The title
#    follows the Community Repository's convention for a new package.
wingetcreate submit --prtitle "New package: richie-rich90454.agent-db-doctor version 1.0.0" `
    --token <GitHub token with the public_repo scope> .\packaging\winget
```

`wingetcreate submit` copies the three files into
`manifests/r/richie-rich90454/agent-db-doctor/<version>/` on a branch of your
fork and opens the pull request; the tools are the wingetcreate binary or,
equivalently, `winget install wingetcreate` on Windows. Doing it by hand is the
same three files copied into that folder on a fork of `microsoft/winget-pkgs`,
with a pull request titled:

```
New package: richie-rich90454.agent-db-doctor version 1.0.0
```

Validation by the repository's own pipeline runs on the pull request, and a
reviewer merges it; after that `winget install richie-rich90454.agent-db-doctor`
works for everyone. Later releases use `wingetcreate update` (or the same
submission flow with the new version) and a pull request titled `Update:
richie-rich90454.agent-db-doctor version <version>`.

## Homebrew

Homebrew installs from a tap: a public repository named `homebrew-tap` under the
account, whose `Formula` folder holds ruby files. Tap formulae may install a
released binary; homebrew-core, the shared catalogue, builds from source, so it
is a later step if the project ever grows into it.

One time setup: a tap is an ordinary public repository named `homebrew-tap` under the
account, with the formula in `Formula/`. This one exists at
`richie-rich90454/homebrew-tap`, and the 1.0.0 formula is published in it.

Release day, once the macOS arm64 tarball and the Linux x86_64 tarball are
attached to the release:

```powershell
# 1. Fill the formula template from the two downloaded tarballs.
.\packaging\generate-homebrew-formula.ps1 -Version 1.0.0 `
    -MacosTarGz .\agent-db-doctor-1.0.0-macos-arm64.tar.gz `
    -LinuxTarGz .\agent-db-doctor-1.0.0-linux-x86_64-gcc.tar.gz
```

```bash
# 2. Put the finished formula in the tap, in a clone of homebrew-tap.
mkdir -p Formula
cp /path/to/agent-db-doctor.rb Formula/agent-db-doctor.rb
git add Formula/agent-db-doctor.rb
git commit -m "agent-db-doctor 1.0.0"
git push
```

Users then install with two commands:

```bash
brew tap richie-rich90454/tap
brew install agent-db-doctor
```

The formula serves macOS on Apple silicon and Linux on x86_64, which are the
platforms the release builds exist for.
