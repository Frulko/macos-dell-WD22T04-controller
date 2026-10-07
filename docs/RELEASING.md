# Build, release and repository maintenance

## Continuous integration

`.github/workflows/ci.yml` runs on main pushes and pull requests. It builds on
`macos-14` (ARM64) and `macos-15-intel` (x86_64), runs `make check`, validates
formula Ruby syntax and uploads packaged artifacts. Tests have no attached dock
and do not prove physical thermal compatibility. Deployment target is macOS 13.
No Homebrew dependency is needed for the resulting executable.

## Publish a version

Use an immutable semantic-version tag. Ensure main checks pass first:

```sh
gh auth status
git pull --ff-only
git tag -a v0.2.0 -m 'dockctl v0.2.0'
git push origin v0.2.0
gh run list --workflow release.yml --limit 5
# Use the run ID from the previous command:
gh run watch RUN_ID --exit-status
```

The release workflow builds/tests both architectures, packages CLI/library/headers
with licenses, ad-hoc signs the executable/dylib, publishes archives and SHA-256
files through `gh release create`, then pins `Formula/dockctl.rb` to the tag's
source archive checksum and pushes that update to main. This needs repository
Actions enabled and `contents: write` for the publish job. Protected main branches
may require adapting that final push to a pull request.

Do not move an already published tag: Homebrew verifies the source checksum.
The source formula is built locally; the downloadable archives are not Homebrew
bottles. No external tap repository, secret signing key or Developer ID is required.

If publication succeeds but the formula update fails, repair it locally:

```sh
python3 scripts/update-formula.py v0.2.0
ruby -c Formula/dockctl.rb
git add Formula/dockctl.rb
git commit -m 'brew: pin dockctl v0.2.0'
git push origin main
```

If binaries need replacing before publication, rerun the failed build job. If a
release was already created, inspect its assets before retrying publication;
`gh release create` intentionally does not overwrite an existing release.

## Update the repository with gh

```sh
gh repo view Frulko/macos-dell-WD22T04-controller
git add README.md docs include lib dockctl.c tests examples Makefile
git commit -m 'Describe the concrete change'
git push origin main
gh run list --limit 5
gh release list
```

Keep local firmware/installers, extraction outputs, hardware identifiers and
build products out of commits. `.gitignore` excludes vendor binaries and generated
disassembly. The vendored macvdmtool reference is included as files with its
license, not a submodule, so fresh checkouts build without another clone.
